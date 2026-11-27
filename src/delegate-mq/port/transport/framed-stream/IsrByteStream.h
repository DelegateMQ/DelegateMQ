#ifndef DMQ_ISR_BYTE_STREAM_H
#define DMQ_ISR_BYTE_STREAM_H

/// @file IsrByteStream.h
/// @see https://github.com/DelegateMQ/DelegateMQ
///
/// @brief The receive side of an interrupt-driven UART (or USB-CDC, or any
/// driver that delivers bytes in a callback), for an IByteStream adapter: the
/// driver's receive interrupt calls PutFromIsr(), and the adapter's Read()
/// calls Read() here, which sleeps until bytes arrive.
///
/// A single-producer, single-consumer ring: one interrupt (or driver callback)
/// puts, one thread reads. No locks between them, so PutFromIsr() is safe in an
/// interrupt. A full ring drops the byte and counts it (GetOverflowCount()):
/// the frame it belonged to then fails its CRC. Non-zero means
/// DMQ_UART_RX_RING_SIZE is too small for the bursts, or the receiving thread
/// doesn't keep up.
///
/// Read() sleeps on a dmq::Semaphore, which every RTOS port gives from an
/// interrupt. Bare metal (DMQ_THREAD_NONE) has none, so Read() polls there
/// until the timeout.
///
/// Needs lock-free 32-bit atomics (any 32-bit MCU).

#include "delegate/DelegateOpt.h"
#if defined(DMQ_HAS_SEMAPHORE)
#include "delegate/Semaphore.h"
#endif
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace dmq::transport {

/// @tparam RingSize received bytes held between reads (a power of 2)
template <uint32_t RingSize = static_cast<uint32_t>(dmq::UART_RX_RING_SIZE)>
class IsrByteStream
{
    static_assert(RingSize >= 2 && (RingSize & (RingSize - 1)) == 0, "RingSize must be a power of 2");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "IsrByteStream needs lock-free 32-bit atomics");

public:
    /// Add one received byte. Interrupt-safe; one producer only.
    void PutFromIsr(uint8_t b)
    {
        Push(b);
        Wake();
    }

    /// Add received bytes (a driver that delivers a block). Interrupt-safe;
    /// one producer only.
    void PutFromIsr(const uint8_t* data, size_t len)
    {
        for (size_t i = 0; i < len; i++)
            Push(data[i]);
        if (len > 0)
            Wake();
    }

    /// The bytes that have arrived, up to len, waiting at most timeout for the
    /// first. One reader only.
    /// @return the number of bytes read, 0 if none arrived in time
    int Read(uint8_t* buf, size_t len, dmq::Duration timeout)
    {
        if (len == 0)
            return 0;
        // Clock::now() + Duration::max() would overflow: cap "forever" at 1000
        // hours, the value of dmq::WAIT_INFINITE (the async-wait delegates')
        const auto forever = std::chrono::duration_cast<dmq::Duration>(std::chrono::hours(1000));
        if (timeout > forever)
            timeout = forever;
        size_t n = Take(buf, len);
        if (n > 0)
            return static_cast<int>(n);
#if defined(DMQ_HAS_SEMAPHORE)
        // A byte may have arrived between Take() and Wait(): the semaphore was
        // given for it, so Wait() returns at once and the loop takes it
        const auto deadline = dmq::Clock::now() + timeout;
        for (;;) {
            const auto now = dmq::Clock::now();
            if (now >= deadline)
                return 0;
            m_dataReady.Wait(deadline - now);
            n = Take(buf, len);
            if (n > 0)
                return static_cast<int>(n);
        }
#else
        // Bare metal: no semaphore to sleep on, so poll
        const auto deadline = dmq::Clock::now() + timeout;
        while (dmq::Clock::now() < deadline) {
            n = Take(buf, len);
            if (n > 0)
                return static_cast<int>(n);
        }
        return 0;
#endif
    }

    /// Drop everything received but not read. Only while the producer is
    /// quiet (its interrupt off), e.g. when (re)starting reception.
    void Clear() { m_tail.store(m_head.load()); }

    /// Bytes dropped because the ring was full.
    uint32_t GetOverflowCount() const { return m_overflows.load(std::memory_order_relaxed); }

private:
    void Push(uint8_t b)
    {
        const uint32_t head = m_head.load(std::memory_order_relaxed);
        const uint32_t tail = m_tail.load(std::memory_order_acquire);
        if (head - tail >= RingSize) {
            m_overflows.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        m_ring[head & (RingSize - 1)] = b;
        m_head.store(head + 1, std::memory_order_release);
    }

    size_t Take(uint8_t* buf, size_t len)
    {
        const uint32_t tail = m_tail.load(std::memory_order_relaxed);
        const uint32_t head = m_head.load(std::memory_order_acquire);
        size_t n = 0;
        while (n < len && tail + n != head) {
            buf[n] = m_ring[(tail + n) & (RingSize - 1)];
            ++n;
        }
        m_tail.store(tail + static_cast<uint32_t>(n), std::memory_order_release);
        return n;
    }

    void Wake()
    {
#if defined(DMQ_HAS_SEMAPHORE)
        m_dataReady.Signal();
#endif
    }

    uint8_t m_ring[RingSize] = {};
    std::atomic<uint32_t> m_head{0};   ///< Written by the producer (free-running)
    std::atomic<uint32_t> m_tail{0};   ///< Written by the reader (free-running)
    std::atomic<uint32_t> m_overflows{0};
#if defined(DMQ_HAS_SEMAPHORE)
    dmq::Semaphore m_dataReady;
#endif
};

} // namespace dmq::transport

#endif // DMQ_ISR_BYTE_STREAM_H
