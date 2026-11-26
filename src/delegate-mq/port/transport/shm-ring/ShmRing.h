#ifndef DMQ_SHM_RING_H
#define DMQ_SHM_RING_H

/// @file ShmRing.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Shared-memory layout for ShmRingTransport: two lock-free
/// single-producer, single-consumer rings of fixed-size message slots, one per
/// direction, in a control block both sides map.
///
/// @details The block is plain memory shared by two cores (e.g. STM32H755
/// M7 + M4, in SRAM placed by both linker scripts) or two processes (a
/// CreateFileMapping / shm_open region), or by two threads in a unit test. No
/// OS calls and no locks between the sides: each index has one writer, and
/// acquire/release ordering on the indices publishes the slot contents.
///
/// - Side A owns `aToB.head` and `bToA.tail`; side B owns `bToA.head` and `aToB.tail`.
/// - Indices are free-running 32-bit counts; `index & (SLOT_COUNT - 1)` picks the
///   slot, and `head - tail` (unsigned, wrap-safe) is the number of full slots.
/// - Side A initializes the block (`Init()`); side B checks it (`IsCompatible()`)
///   before using it. Both sides must be built with the same SlotCount and
///   MaxPayload; the block records both so a mismatch is caught, not misread.
/// - Caches: the region must be coherent between the sides. Two cores where one
///   has a data cache (the H755's M7) need the region marked non-cacheable (MPU).
///
/// Include DelegateOpt.h (or ShmRingTransport.h) rather than this header alone
/// where possible; it needs only <atomic> and <cstdint>.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "port/transport/common/DmqHeader.h"

namespace dmq::transport {

/// Cache line size the indices are padded to, so the producer's and consumer's
/// index writes never share a line. 64 covers x86/x64 and Cortex-A; the
/// Cortex-M7's lines are 32. A layout detail, not a tuning knob.
inline constexpr size_t SHM_CACHE_LINE = 64;

static_assert(std::atomic<uint32_t>::is_always_lock_free,
    "ShmRing needs lock-free 32-bit atomics: a lock-based atomic doesn't work across cores or processes");

/// @brief One message: the 10-byte wire header, then up to MaxPayload bytes.
/// Fixed size, so a message is always contiguous (no wrap-around split).
template <size_t MaxPayload>
struct ShmSlot
{
    uint8_t header[DmqHeader::HEADER_SIZE];
    uint8_t payload[MaxPayload];
};

/// @brief A single-producer, single-consumer ring of message slots.
template <size_t SlotCount, size_t MaxPayload>
struct ShmRing
{
    static_assert(SlotCount >= 2 && (SlotCount & (SlotCount - 1)) == 0,
        "ShmRing: SlotCount must be a power of 2, at least 2");
    static_assert(SlotCount <= 0x80000000u, "ShmRing: SlotCount too large for 32-bit indices");

    static constexpr uint32_t MASK = static_cast<uint32_t>(SlotCount - 1);

    alignas(SHM_CACHE_LINE) std::atomic<uint32_t> head{ 0 };   ///< Next slot to write; producer only
    alignas(SHM_CACHE_LINE) std::atomic<uint32_t> tail{ 0 };   ///< Next slot to read; consumer only
    alignas(SHM_CACHE_LINE) ShmSlot<MaxPayload> slots[SlotCount];

    /// Empty the ring. Only while neither side is using it.
    void Reset() {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }
};

/// @brief The shared region: a header both sides check, then one ring per direction.
template <size_t SlotCount, size_t MaxPayload>
struct ShmControlBlock
{
    static constexpr uint32_t MAGIC = 0x444D5153;    ///< "DMQS"
    static constexpr uint32_t VERSION = 1;           ///< Layout version

    using Ring = ShmRing<SlotCount, MaxPayload>;

    std::atomic<uint32_t> magic{ 0 };       ///< MAGIC once side A has initialized the block
    uint32_t version = 0;                   ///< VERSION
    uint32_t slotCount = 0;                 ///< SlotCount side A was built with
    uint32_t maxPayload = 0;                ///< MaxPayload side A was built with
    std::atomic<uint32_t> readyA{ 0 };      ///< 1 while side A's transport is open
    std::atomic<uint32_t> readyB{ 0 };      ///< 1 while side B's transport is open
    std::atomic<uint32_t> ownerA{ 0 };      ///< Set by a platform layer: who holds side A (a process ID on desktop); 0 = unknown

    Ring aToB;                              ///< Side A -> side B
    Ring bToA;                              ///< Side B -> side A

    /// @brief Side A: initialize the block, emptying both rings. Call before side
    /// B attaches (on a dual-core MCU, before releasing the other core). Writes
    /// the magic last, with release ordering, so a side B that sees it sees the rest.
    void Init() {
        magic.store(0, std::memory_order_relaxed);
        version = VERSION;
        slotCount = static_cast<uint32_t>(SlotCount);
        maxPayload = static_cast<uint32_t>(MaxPayload);
        readyA.store(0, std::memory_order_relaxed);
        readyB.store(0, std::memory_order_relaxed);
        ownerA.store(0, std::memory_order_relaxed);
        aToB.Reset();
        bToA.Reset();
        magic.store(MAGIC, std::memory_order_release);
    }

    /// @brief Side B: true once side A initialized the block with the same layout.
    bool IsCompatible() const {
        return magic.load(std::memory_order_acquire) == MAGIC &&
            version == VERSION &&
            slotCount == static_cast<uint32_t>(SlotCount) &&
            maxPayload == static_cast<uint32_t>(MaxPayload);
    }
};

} // namespace dmq::transport

#endif // DMQ_SHM_RING_H
