#ifndef DMQ_SHM_RING_TRANSPORT_H
#define DMQ_SHM_RING_TRANSPORT_H

/// @file ShmRingTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Shared-memory transport: DelegateMQ messages between two cores of an
/// AMP MCU (e.g. STM32H755 M7 + M4), two processes, or two threads, through a
/// pair of lock-free rings (ShmRing.h).
///
/// @details
/// **Layout.** Both sides point at one ShmControlBlock: side A sends on `aToB`
/// and receives on `bToA`, side B the reverse. Each ring has exactly one
/// producer and one consumer, so the sides never lock each other. Hardware-
/// and OS-agnostic: no OS calls here; where the memory lives and how the peer
/// is woken are the application's (or a port's) choice.
///
/// **Waking the peer: IShmSignal.** After a Send() publishes a message it calls
/// `NotifyPeer()`, e.g. an HSEM release that interrupts the other core, a
/// named event between processes, a condition variable between threads. A
/// Receive() that finds the ring empty calls `WaitForData(timeout)` (the
/// receive timeout, `SetRecvTimeout()`), so a receive thread sleeps until data
/// arrives instead of polling. With no signal, Send() wakes nobody and Receive()
/// never blocks: poll it, or drain from an interrupt-driven `IsrDelegate`.
///
/// **One message per slot.** Header and payload are copied into a fixed-size
/// slot, so a message never wraps around the ring and framing can't be lost.
/// Receive() copies the payload out to the transport's own buffer and releases
/// the slot at once, so the sender isn't held up while the message is dispatched.
///
/// **Full ring.** Send() never waits: a full ring, or a payload over MaxPayload,
/// fails the Send() (returns -1, and a remote delegate reports the error) and is
/// counted (`GetSendFullCount()`, `GetSendTooLargeCount()`). Size the ring for
/// the bursts expected between receives (DMQ_SHM_SLOT_COUNT).
///
/// **ACKs.** As with the UART and UDP transports: a received message other than
/// an ACK is ACKed through the send transport (this one by default), and a
/// received ACK clears the entry in the TransportMonitor, if one is set.
/// ACKs use ring slots too; a link that doesn't use a TransportMonitor can
/// `SetSendTransport(nullptr)` on the receiving side to send none.
///
/// **Threads.** Send() may be called from several threads on one side (a send
/// lock serializes them, on that side only). Receive() is for one thread per
/// side, as with any transport, and is serialized by its own lock.
///
/// @code
///   // Both sides, same build settings:
///   using Shm = dmq::transport::ShmRingTransport<>;
///   Shm::ControlBlock& block = ...;  // shared memory (linker section, mapping)
///
///   // Side A (initializes the block):
///   Shm a; a.Create(block, Shm::Side::A, &signalA);
///   // Side B (after side A, e.g. once the other core is released):
///   Shm b; b.Create(block, Shm::Side::B, &signalB);   // -1 until A initialized it
/// @endcode
///
/// @note Header-only and free of OS calls, so it builds on every port, bare
/// metal included. Uses `dmq::Mutex` for the per-side locks (`NullMutex` on bare
/// metal). Not included by DelegateMQ.h (it can sit beside any DMQ_TRANSPORT_*,
/// and needs lock-free 32-bit atomics, which AVR lacks): include this header.

#include "delegate/DelegateOpt.h"
#include "port/transport/common/DmqHeader.h"
#include "port/transport/common/ITransport.h"
#include "port/transport/common/ITransportMonitor.h"
#include "ShmRing.h"
#include <atomic>
#include <chrono>
#include <cstring>

namespace dmq::transport {

/// @brief How a ShmRingTransport wakes its peer and waits for data. One per
/// side: NotifyPeer() rings the peer's doorbell, WaitForData() waits on this
/// side's.
class IShmSignal
{
public:
    virtual ~IShmSignal() = default;

    /// Wake the peer: a message was just published to it. Called by Send()
    /// after the message is visible to the peer. Must not block.
    virtual void NotifyPeer() = 0;

    /// Wait until the peer notifies this side or the timeout passes. A
    /// notification that came before the call must end the wait at once (a
    /// latched event or semaphore, not an edge that can be missed). Spurious
    /// returns are fine: Receive() checks the ring again.
    /// @return true if notified, false on timeout.
    virtual bool WaitForData(std::chrono::milliseconds timeout) = 0;
};

/// @brief ITransport over a pair of shared-memory rings. See the file comment.
/// @tparam SlotCount  Slots per direction, a power of 2. Both sides must match.
/// @tparam MaxPayload Largest payload in bytes. Both sides must match.
template <size_t SlotCount = dmq::SHM_SLOT_COUNT, size_t MaxPayload = dmq::SHM_MAX_PAYLOAD>
class ShmRingTransport : public ITransport
{
    static_assert(MaxPayload > 0 && MaxPayload <= 65535,
        "ShmRingTransport: MaxPayload must be 1 to 65535 (the header's length is 16 bits)");

public:
    using ControlBlock = ShmControlBlock<SlotCount, MaxPayload>;
    using Ring = typename ControlBlock::Ring;

    /// Which end of the block this transport is. Side A initializes the block.
    enum class Side { A, B };

    ShmRingTransport() = default;
    ~ShmRingTransport() override { Close(); }

    ShmRingTransport(const ShmRingTransport&) = delete;
    ShmRingTransport& operator=(const ShmRingTransport&) = delete;

    /// @brief Open this side of the link.
    /// @details Side A initializes the block, emptying both rings, so create
    /// side A before side B uses the block (and not again while B is running).
    /// Side B fails until side A has initialized it with the same SlotCount and
    /// MaxPayload; retry until it succeeds.
    /// @param[in] block  The shared control block. Must outlive the transport.
    /// @param[in] side   This side.
    /// @param[in] signal Wakes the peer and waits for data; nullptr to poll.
    /// Must outlive the transport.
    /// @return 0 on success, -1 if side B found the block uninitialized or
    /// built with another layout.
    int Create(ControlBlock& block, Side side, IShmSignal* signal = nullptr)
    {
        const dmq::LockGuard<dmq::Mutex> recvLock(m_recvMutex);
        const dmq::LockGuard<dmq::Mutex> sendLock(m_sendMutex);
        CloseLocked();

        if (side == Side::A)
            block.Init();
        else if (!block.IsCompatible())
            return -1;

        m_block = &block;
        m_side = side;
        m_signal = signal;
        m_tx = (side == Side::A) ? &block.aToB : &block.bToA;
        m_rx = (side == Side::A) ? &block.bToA : &block.aToB;
        ReadyFlag(side).store(1, std::memory_order_release);
        return 0;
    }

    /// @brief Close this side. The block and anything still in the rings are
    /// left as they are. Safe to call more than once.
    /// @note Waits for a Receive() in progress on another thread, which can be
    /// blocked in WaitForData() for up to the receive timeout.
    void Close()
    {
        const dmq::LockGuard<dmq::Mutex> recvLock(m_recvMutex);
        const dmq::LockGuard<dmq::Mutex> sendLock(m_sendMutex);
        CloseLocked();
    }

    /// @brief True if this side is open.
    bool IsOpen() const
    {
        const dmq::LockGuard<dmq::Mutex> lock(m_sendMutex);
        return m_block != nullptr;
    }

    /// @brief True if the peer's side is open (it called Create() and not Close()).
    bool IsPeerOpen() const
    {
        const dmq::LockGuard<dmq::Mutex> lock(m_sendMutex);
        if (!m_block) return false;
        return ReadyFlag(m_side == Side::A ? Side::B : Side::A).load(std::memory_order_acquire) != 0;
    }

    /// @brief The most Receive() blocks for data when a signal is set (ignored
    /// without one). 0 = never block. Default DMQ_SHM_RECV_TIMEOUT_MS.
    /// Negative counts as 0; capped at 0xFFFFFFFE ms (Win32 treats 0xFFFFFFFF as forever).
    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        const auto ms = timeout.count();
        const uint32_t clamped = ms <= 0 ? 0u : (ms >= 0xFFFFFFFELL ? 0xFFFFFFFEu : static_cast<uint32_t>(ms));
        m_recvTimeoutMs.store(clamped, std::memory_order_relaxed);
    }

    /// @brief The monitor a received ACK is reported to (Remove()).
    void SetTransportMonitor(ITransportMonitor* tm)
    {
        const dmq::LockGuard<dmq::Mutex> lock(m_recvMutex);
        m_transportMonitor = tm;
    }

    /// @brief Where ACKs for received messages are sent: this transport by
    /// default; nullptr sends none.
    void SetSendTransport(ITransport* st)
    {
        const dmq::LockGuard<dmq::Mutex> lock(m_recvMutex);
        m_sendTransport = st;
    }

    /// @brief Send() calls that failed because the peer's ring was full.
    uint32_t GetSendFullCount() const { return m_sendFull.load(std::memory_order_relaxed); }

    /// @brief Send() calls that failed because the payload exceeded MaxPayload.
    uint32_t GetSendTooLargeCount() const { return m_sendTooLarge.load(std::memory_order_relaxed); }

    /// @brief Received slots dropped as malformed (bad marker, version or length).
    uint32_t GetRecvErrorCount() const { return m_recvErrors.load(std::memory_order_relaxed); }

    /// @brief Publish a message to the peer's ring and notify it. Never blocks
    /// on the peer.
    /// @return 0 on success; -1 if closed, `os` failed, the payload exceeds
    /// MaxPayload, or the ring is full.
    int Send(const dmq::ByteWriter& os, const DmqHeader& header) override
    {
        if (os.fail())
            return -1;

        {
            // NotifyPeer() runs under the lock too, so Close() can't release
            // the signal's resources while a Send() is still using them
            const dmq::LockGuard<dmq::Mutex> lock(m_sendMutex);
            if (!m_tx)
                return -1;

            const size_t len = os.size();
            if (len > MaxPayload) {
                m_sendTooLarge.fetch_add(1, std::memory_order_relaxed);
                return -1;
            }

            // Producer: head is ours (relaxed); acquire on tail so the consumer
            // has finished reading a slot before it is reused.
            const uint32_t head = m_tx->head.load(std::memory_order_relaxed);
            const uint32_t tail = m_tx->tail.load(std::memory_order_acquire);
            if (static_cast<uint32_t>(head - tail) >= static_cast<uint32_t>(SlotCount)) {
                m_sendFull.fetch_add(1, std::memory_order_relaxed);
                return -1;
            }

            ShmSlot<MaxPayload>& slot = m_tx->slots[head & Ring::MASK];
            DmqHeader wire = header;
            wire.SetLength(static_cast<uint16_t>(len));
            wire.Encode(slot.header);
            if (len > 0)
                memcpy(slot.payload, os.data(), len);

            // Release: the slot's contents are visible before the new head
            m_tx->head.store(head + 1, std::memory_order_release);
            if (m_signal)
                m_signal->NotifyPeer();
        }
        return 0;
    }

    /// @brief Take the next message from this side's ring.
    /// @details With a signal and a non-zero receive timeout, waits for the peer
    /// when the ring is empty. The payload is copied to this transport's buffer
    /// (valid until the next Receive() or Close()) and the slot freed at once.
    /// @return 0 with a message, RECEIVE_NO_DATA if none arrived, -1 if closed
    /// or the slot was malformed (dropped and counted).
    int Receive(dmq::ByteReader& is, DmqHeader& header) override
    {
        is.assign(nullptr, 0);   // nothing until a message arrives
        uint16_t len = 0;
        ITransportMonitor* monitor = nullptr;
        ITransport* ackTransport = nullptr;
        {
            const dmq::LockGuard<dmq::Mutex> recvLock(m_recvMutex);
            if (!m_rx)
                return -1;

            // Consumer: tail is ours (relaxed); acquire on head so the slot's
            // contents written before it are visible.
            const uint32_t tail = m_rx->tail.load(std::memory_order_relaxed);
            uint32_t head = m_rx->head.load(std::memory_order_acquire);
            if (head == tail) {
                WaitForMessage(tail);
                head = m_rx->head.load(std::memory_order_acquire);
                if (head == tail)
                    return RECEIVE_NO_DATA;
            }

            const ShmSlot<MaxPayload>& slot = m_rx->slots[tail & Ring::MASK];
            header.Decode(slot.header);
            len = header.GetLength();
            const bool valid = header.IsValid() && len <= MaxPayload;
            if (valid && len > 0)
                memcpy(m_rxBuf, slot.payload, len);

            // Release: done with the slot; the producer may reuse it
            m_rx->tail.store(tail + 1, std::memory_order_release);

            if (!valid) {
                m_recvErrors.fetch_add(1, std::memory_order_relaxed);
                return -1;
            }
            monitor = m_transportMonitor;
            ackTransport = m_sendTransport;
        }

        if (len > 0)
            is.assign(reinterpret_cast<const char*>(m_rxBuf), len);

        // Outside the receive lock: TransportMonitor::Remove() fires
        // OnSendStatus, whose subscribers may call back into this transport
        if (header.GetId() == dmq::ACK_REMOTE_ID) {
            if (monitor)
                monitor->Remove(header.GetSeqNum());
        }
        else if (ackTransport) {
            // A failed ACK (full ring) is counted by Send(); the sender's
            // TransportMonitor then times the message out.
            const dmq::ByteWriter empty;
            const DmqHeader ack(dmq::ACK_REMOTE_ID, header.GetSeqNum());
            ackTransport->Send(empty, ack);
        }
        return 0;
    }

private:
    /// Ring empty: wait for the peer, up to the receive timeout, if a signal is
    /// set. A signal can be left set by a message that was taken without
    /// waiting; such a wake-up finds the ring still empty, so wait out the rest
    /// of the timeout rather than return early. The caller checks the ring again.
    void WaitForMessage(uint32_t tail)
    {
        const uint32_t timeoutMs = m_recvTimeoutMs.load(std::memory_order_relaxed);
        if (!m_signal || timeoutMs == 0)
            return;
        const auto deadline = dmq::Clock::now() + std::chrono::milliseconds(timeoutMs);
        std::chrono::milliseconds remaining(timeoutMs);
        for (;;) {
            if (!m_signal->WaitForData(remaining))
                return;                                         // timed out
            if (m_rx->head.load(std::memory_order_acquire) != tail)
                return;                                         // data
            const auto now = dmq::Clock::now();
            if (now >= deadline)
                return;
            remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
        }
    }

    std::atomic<uint32_t>& ReadyFlag(Side side) const
    {
        return (side == Side::A) ? m_block->readyA : m_block->readyB;
    }

    void CloseLocked()
    {
        if (m_block)
            // seq_cst: a derived Close() then reads the peer's flag to decide
            // who cleans up; both closing at once must not both see "peer open"
            ReadyFlag(m_side).store(0, std::memory_order_seq_cst);
        m_block = nullptr;
        m_tx = nullptr;
        m_rx = nullptr;
        m_signal = nullptr;
    }

    ControlBlock* m_block = nullptr;
    Side m_side = Side::A;
    Ring* m_tx = nullptr;               // the peer's ring: this side produces
    Ring* m_rx = nullptr;               // this side's ring: this side consumes
    IShmSignal* m_signal = nullptr;

    ITransport* m_sendTransport = this;
    ITransportMonitor* m_transportMonitor = nullptr;

    std::atomic<uint32_t> m_recvTimeoutMs{ static_cast<uint32_t>(dmq::SHM_RECV_TIMEOUT.count()) };
    std::atomic<uint32_t> m_sendFull{ 0 };
    std::atomic<uint32_t> m_sendTooLarge{ 0 };
    std::atomic<uint32_t> m_recvErrors{ 0 };

    // Lock order: m_recvMutex, then m_sendMutex (Receive() sends ACKs)
    mutable dmq::Mutex m_sendMutex;     // Send() callers on this side, and the open state
    dmq::Mutex m_recvMutex;             // Receive(); Create()/Close() take both

    uint8_t m_rxBuf[MaxPayload]{};      // the last received payload
};

} // namespace dmq::transport

#endif // DMQ_SHM_RING_TRANSPORT_H
