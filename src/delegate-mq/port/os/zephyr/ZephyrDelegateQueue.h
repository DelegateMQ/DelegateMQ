#ifndef ZEPHYR_DELEGATE_QUEUE_H
#define ZEPHYR_DELEGATE_QUEUE_H

/// @file ZephyrDelegateQueue.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Thin RAII wrapper around a pair of Zephyr k_msgq primitives used by
/// dmq::os::Thread to carry pending ThreadMsg pointers, with priority support.
///
/// @details
/// Zephyr's k_msgq has no native "send to front" / priority-put primitive
/// (unlike FreeRTOS's xQueueSendToFront, ThreadX's tx_queue_front_send, or
/// CMSIS-RTOS2's osMessageQueuePut msg_prio argument) -- a single k_msgq is
/// strict FIFO, which previously meant dmq::Priority::HIGH was silently
/// treated as NORMAL on this port. To give HIGH the same "jumps the FIFO"
/// semantics here as on every other RTOS port, this wrapper keeps two
/// independent k_msgq instances (mirroring the desktop stdlib/win32/qt ports'
/// two-deque model) and Receive() always drains the high-priority queue
/// first.
///
/// This doubles queue memory relative to a single-k_msgq design (each lane
/// gets its own 'capacity'-sized buffer, so worst-case combined depth is
/// 2x capacity rather than exactly capacity) and Receive() uses k_poll() to
/// block efficiently on whichever queue gets data first, rather than a
/// single native blocking receive. This class owns no policy decisions of
/// its own (FullPolicy, timeout computation, and stats stay in Thread.cpp)
/// -- it only knows how to move ThreadMsg* pointers through the two queues.
///
/// @warning Unverified: written against the documented Zephyr k_msgq/k_poll
/// API but never compiled or run against a real Zephyr SDK/west workspace
/// (none is available in this development environment). The k_poll()-based
/// dual-queue wait is more novel API surface than the plain k_msgq_put/get
/// calls used elsewhere in this port, so review this file with extra care
/// and exercise it on a real target or simulator before relying on it in
/// production -- consistent with the existing caveat on this port's
/// CriticalSection implementation.

#include <zephyr/kernel.h>
#include "port/os/common/ThreadMsg.h"
#include <memory>
#include <cstring>

namespace dmq::os {

class ZephyrDelegateQueue
{
public:
    ZephyrDelegateQueue()
    {
        memset(&m_highMsgq, 0, sizeof(m_highMsgq));
        memset(&m_normalMsgq, 0, sizeof(m_normalMsgq));
    }
    ~ZephyrDelegateQueue() { Destroy(); }

    ZephyrDelegateQueue(const ZephyrDelegateQueue&) = delete;
    ZephyrDelegateQueue& operator=(const ZephyrDelegateQueue&) = delete;

    /// Create both queues, each sized to hold up to 'capacity' pointers.
    /// @return true on success, false if either buffer allocation failed.
    bool Create(size_t capacity)
    {
        if (m_created)
            return true;

        size_t bufBytes = MSG_SIZE * capacity;

        char* highBuf = (char*)k_aligned_alloc(sizeof(void*), bufBytes);
        if (!highBuf)
            return false;
        m_highBuffer.reset(highBuf);
        k_msgq_init(&m_highMsgq, m_highBuffer.get(), MSG_SIZE, capacity);

        char* normalBuf = (char*)k_aligned_alloc(sizeof(void*), bufBytes);
        if (!normalBuf) {
            m_highBuffer.reset();
            return false;
        }
        m_normalBuffer.reset(normalBuf);
        k_msgq_init(&m_normalMsgq, m_normalBuffer.get(), MSG_SIZE, capacity);

        m_created = true;
        return true;
    }

    bool IsCreated() const { return m_created; }

    /// Send a message. 'highPriority' routes to the high-priority queue,
    /// which Receive() always drains first; otherwise the normal queue.
    /// 'waitOption' is K_NO_WAIT for non-blocking, K_FOREVER to block
    /// forever, or a finite k_timeout_t for a bounded wait -- applied only
    /// to the one queue the message is routed to (each lane has its own
    /// independent capacity; there is no combined-depth wait across both).
    /// @return true if the message was enqueued.
    bool Send(ThreadMsg* msg, bool highPriority, k_timeout_t waitOption)
    {
        struct k_msgq* q = highPriority ? &m_highMsgq : &m_normalMsgq;
        return k_msgq_put(q, &msg, waitOption) == 0;
    }

    /// Receive a message pointer, blocking up to 'waitOption'. The
    /// high-priority queue is always checked -- and drained -- first, so a
    /// pending HIGH message is returned even if NORMAL messages arrived
    /// earlier. Uses k_poll() to block efficiently on both queues at once
    /// rather than polling in a loop.
    /// @return the message, or nullptr on timeout.
    ThreadMsg* Receive(k_timeout_t waitOption)
    {
        ThreadMsg* msg = nullptr;

        // Fast path: don't pay for k_poll() if something is already pending.
        if (k_msgq_get(&m_highMsgq, &msg, K_NO_WAIT) == 0)
            return msg;
        if (k_msgq_get(&m_normalMsgq, &msg, K_NO_WAIT) == 0)
            return msg;

        if (K_TIMEOUT_EQ(waitOption, K_NO_WAIT))
            return nullptr;

        struct k_poll_event events[2];
        k_poll_event_init(&events[0], K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &m_highMsgq);
        k_poll_event_init(&events[1], K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &m_normalMsgq);

        if (k_poll(events, 2, waitOption) != 0)
            return nullptr; // timed out waiting for either queue

        // k_poll only signals readiness; re-check high first so a HIGH
        // message still jumps the FIFO even if NORMAL became ready too.
        if (k_msgq_get(&m_highMsgq, &msg, K_NO_WAIT) == 0)
            return msg;
        if (k_msgq_get(&m_normalMsgq, &msg, K_NO_WAIT) == 0)
            return msg;
        return nullptr;
    }

    /// Combined number of messages waiting across both queues.
    size_t Size()
    {
        if (!m_created)
            return 0;
        return static_cast<size_t>(k_msgq_num_used_get(&m_highMsgq))
             + static_cast<size_t>(k_msgq_num_used_get(&m_normalMsgq));
    }

    /// Remove and delete every pending message in both queues without
    /// blocking. Used during shutdown once the owning thread has stopped
    /// consuming the queues.
    void DrainAndDelete()
    {
        ThreadMsg* msg = nullptr;
        while (k_msgq_get(&m_highMsgq, &msg, K_NO_WAIT) == 0) {
            delete msg;
        }
        while (k_msgq_get(&m_normalMsgq, &msg, K_NO_WAIT) == 0) {
            delete msg;
        }
    }

    void Destroy()
    {
        m_highBuffer.reset();
        m_normalBuffer.reset();
        m_created = false;
    }

private:
    using ZephyrDeleter = void(*)(void*);

    static const size_t MSG_SIZE = sizeof(ThreadMsg*);

    struct k_msgq m_highMsgq;
    struct k_msgq m_normalMsgq;

    // Dynamically allocated queue buffers, freed via k_free.
    std::unique_ptr<char, ZephyrDeleter> m_highBuffer{nullptr, k_free};
    std::unique_ptr<char, ZephyrDeleter> m_normalBuffer{nullptr, k_free};

    bool m_created = false;
};

} // namespace dmq::os

#endif // ZEPHYR_DELEGATE_QUEUE_H
