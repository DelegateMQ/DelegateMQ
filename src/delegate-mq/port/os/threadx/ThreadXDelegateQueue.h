#ifndef THREADX_DELEGATE_QUEUE_H
#define THREADX_DELEGATE_QUEUE_H

/// @file ThreadXDelegateQueue.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Thin RAII wrapper around a pair of ThreadX TX_QUEUE primitives used
/// by dmq::os::Thread to carry pending ThreadMsg pointers, with priority
/// support. See the Priority enum in delegate/DelegateMsg.h for the
/// cross-port ordering contract this class must uphold.
///
/// @details
/// *** DO NOT "SIMPLIFY" THIS BACK TO ONE QUEUE + tx_queue_front_send() ***
/// An earlier version of this class used a single TX_QUEUE and called
/// tx_queue_front_send() for HIGH priority messages. That is WRONG: sending
/// to the front of a single queue makes HIGH messages a LIFO stack relative
/// to each other, not a FIFO lane. Concretely, starting from [Normal1]:
///   1. HIGH-A sent to front  -> [HIGH-A, Normal1]
///   2. HIGH-B sent to front  -> [HIGH-B, HIGH-A, Normal1]
/// The consumer then dequeues HIGH-B before HIGH-A, even though A arrived
/// first -- exactly backwards. See DelegateMsg.h's Priority comment for the
/// required contract (HIGH jumps ahead of NORMAL, but FIFO is preserved
/// within each priority).
///
/// The fix: two independent TX_QUEUE instances, one per priority lane, each
/// always pushed to the BACK (tx_queue_send, never tx_queue_front_send).
/// ThreadX gives no way to block on "either of two queues has data" the way
/// a std::condition_variable can wait on an arbitrary predicate, so a
/// TX_SEMAPHORE counting semaphore is used as a shared "doorbell": every
/// successful Send() (to either lane) puts the semaphore once; Receive()
/// gets the semaphore (the single blocking wait) and then does a
/// non-blocking pop, checking the HIGH lane first so it still drains ahead
/// of NORMAL. This mirrors StdlibThread's two-std::deque + one-
/// condition_variable design and ZephyrDelegateQueue's two-k_msgq + k_poll
/// design -- same shape, RTOS-native primitives.
///
/// Each lane is sized to the full requested 'capacity' (like
/// ZephyrDelegateQueue), so worst-case combined depth is 2x capacity rather
/// than exactly capacity -- this doubles queue memory relative to the old
/// single-queue design. Unlike FreeRTOS's counting semaphore, ThreadX's
/// tx_semaphore_create() has no "max count" ceiling to size -- a put() only
/// fails on ULONG overflow, which combined lane capacity never approaches.
///
/// Isolates the native tx_queue_create/tx_queue_send/tx_queue_receive/
/// tx_queue_delete/tx_semaphore_create/tx_semaphore_put/tx_semaphore_get
/// calls (and the ULONG-word buffer sizing they require) behind a small type
/// so Thread.cpp's DispatchDelegate()/Run() read as policy and dispatch
/// logic, not ThreadX API plumbing. This class owns no policy decisions of
/// its own (FullPolicy, timeout computation, and stats stay in Thread.cpp)
/// -- it only knows how to move ThreadMsg* pointers through two
/// fixed-capacity ThreadX queues in priority order.

#include <tx_api.h>
#include "port/os/common/ThreadMsg.h"
#include <memory>
#include <new>
#include <cstring>

namespace dmq::os {

class ThreadXDelegateQueue
{
public:
    ThreadXDelegateQueue()
    {
        memset(&m_highQueue, 0, sizeof(m_highQueue));
        memset(&m_normalQueue, 0, sizeof(m_normalQueue));
        memset(&m_countSem, 0, sizeof(m_countSem));
    }
    ~ThreadXDelegateQueue() { Destroy(); }

    ThreadXDelegateQueue(const ThreadXDelegateQueue&) = delete;
    ThreadXDelegateQueue& operator=(const ThreadXDelegateQueue&) = delete;

    /// Create the underlying HIGH and NORMAL queues, each with room for
    /// 'capacity' pointers, plus the counting semaphore doorbell (initial
    /// count 0).
    /// @param name Queue/semaphore name; ThreadX stores this pointer, not a
    ///   copy, so it must outlive the queue (callers pass THREAD_NAME.c_str(),
    ///   which lives as long as the owning Thread). The same name is shared
    ///   across both queues and the semaphore -- ThreadX names are purely
    ///   informational for kernel-aware debuggers and need not be unique.
    /// @return true on success, false if any buffer allocation or ThreadX
    ///   create call failed.
    bool Create(size_t capacity, const char* name)
    {
        if (m_highQueue.tx_queue_id != 0 && m_normalQueue.tx_queue_id != 0 && m_countSem.tx_semaphore_id != 0)
            return true;

        // ThreadX queues store "words" (ULONGs). We're passing a pointer
        // (ThreadMsg*), so round up to enough words to hold one.
        UINT msgSizeWords = (sizeof(ThreadMsg*) + sizeof(ULONG) - 1) / sizeof(ULONG);
        ULONG queueMemSizeWords = static_cast<ULONG>(capacity) * msgSizeWords;

        m_highQueueMemory.reset(new (std::nothrow) ULONG[queueMemSizeWords]);
        m_normalQueueMemory.reset(new (std::nothrow) ULONG[queueMemSizeWords]);
        if (!m_highQueueMemory || !m_normalQueueMemory)
            return false;

        UINT ret = tx_queue_create(&m_highQueue,
                                    const_cast<CHAR*>(name),
                                    msgSizeWords,
                                    m_highQueueMemory.get(),
                                    queueMemSizeWords * sizeof(ULONG));
        if (ret != TX_SUCCESS)
            return false;

        ret = tx_queue_create(&m_normalQueue,
                               const_cast<CHAR*>(name),
                               msgSizeWords,
                               m_normalQueueMemory.get(),
                               queueMemSizeWords * sizeof(ULONG));
        if (ret != TX_SUCCESS)
            return false;

        ret = tx_semaphore_create(&m_countSem, const_cast<CHAR*>(name), 0);
        return ret == TX_SUCCESS;
    }

    bool IsCreated() const
    {
        return m_highQueue.tx_queue_id != 0 && m_normalQueue.tx_queue_id != 0 && m_countSem.tx_semaphore_id != 0;
    }

    /// Send a message pointer. 'highPriority' routes to the HIGH lane, which
    /// Receive() always drains first; otherwise the NORMAL lane. Both lanes
    /// are always pushed to the BACK -- see the file-level comment for why
    /// "send to front" must never be reintroduced here. 'waitOption' is
    /// TX_NO_WAIT for non-blocking, TX_WAIT_FOREVER to block forever, or a
    /// finite tick count for a bounded wait; it applies only to the queue
    /// push (each lane has its own independent capacity -- there is no
    /// combined-depth wait across both). Putting the doorbell semaphore never
    /// blocks and cannot realistically fail (only on ULONG overflow, far
    /// beyond any real combined lane capacity).
    /// @return true if the message was enqueued.
    bool Send(ThreadMsg* msg, bool highPriority, ULONG waitOption)
    {
        TX_QUEUE* q = highPriority ? &m_highQueue : &m_normalQueue;
        if (tx_queue_send(q, &msg, waitOption) != TX_SUCCESS)
            return false;
        tx_semaphore_put(&m_countSem);
        return true;
    }

    /// Receive a message pointer, blocking up to 'waitOption'. The HIGH lane
    /// is always checked -- and drained -- first, so a pending HIGH message
    /// is returned even if NORMAL messages arrived earlier. Blocks on the
    /// doorbell semaphore (the single native wait), then does a
    /// non-blocking pop from whichever lane has data; the semaphore count
    /// guarantees at least one lane is non-empty once the get succeeds.
    /// @return the message, or nullptr on timeout.
    ThreadMsg* Receive(ULONG waitOption)
    {
        if (tx_semaphore_get(&m_countSem, waitOption) != TX_SUCCESS)
            return nullptr; // timed out waiting for either lane

        ThreadMsg* msg = nullptr;
        if (tx_queue_receive(&m_highQueue, &msg, TX_NO_WAIT) == TX_SUCCESS)
            return msg;
        if (tx_queue_receive(&m_normalQueue, &msg, TX_NO_WAIT) == TX_SUCCESS)
            return msg;
        return nullptr;
    }

    /// Combined number of messages waiting across both lanes.
    size_t Size()
    {
        if (m_highQueue.tx_queue_id == 0 || m_normalQueue.tx_queue_id == 0)
            return 0;

        ULONG enqueued = 0;
        ULONG available = 0;
        TX_THREAD* suspension_list = nullptr;
        ULONG suspension_count = 0;
        TX_QUEUE* next_queue = nullptr;
        size_t total = 0;

        if (tx_queue_info_get(&m_highQueue, nullptr, &enqueued, &available,
                               &suspension_list, &suspension_count, &next_queue) == TX_SUCCESS) {
            total += static_cast<size_t>(enqueued);
        }
        if (tx_queue_info_get(&m_normalQueue, nullptr, &enqueued, &available,
                               &suspension_list, &suspension_count, &next_queue) == TX_SUCCESS) {
            total += static_cast<size_t>(enqueued);
        }
        return total;
    }

    /// Remove and delete every pending message in both lanes without
    /// blocking. Used during shutdown once the owning thread has stopped
    /// consuming the queues.
    void DrainAndDelete()
    {
        ThreadMsg* msg = nullptr;
        while (tx_queue_receive(&m_highQueue, &msg, TX_NO_WAIT) == TX_SUCCESS) {
            delete msg;
        }
        while (tx_queue_receive(&m_normalQueue, &msg, TX_NO_WAIT) == TX_SUCCESS) {
            delete msg;
        }
    }

    void Destroy()
    {
        if (m_highQueue.tx_queue_id != 0) {
            tx_queue_delete(&m_highQueue);
            memset(&m_highQueue, 0, sizeof(m_highQueue));
        }
        if (m_normalQueue.tx_queue_id != 0) {
            tx_queue_delete(&m_normalQueue);
            memset(&m_normalQueue, 0, sizeof(m_normalQueue));
        }
        if (m_countSem.tx_semaphore_id != 0) {
            tx_semaphore_delete(&m_countSem);
            memset(&m_countSem, 0, sizeof(m_countSem));
        }
        m_highQueueMemory.reset();
        m_normalQueueMemory.reset();
    }

private:
    TX_QUEUE m_highQueue;
    TX_QUEUE m_normalQueue;
    TX_SEMAPHORE m_countSem;
    std::unique_ptr<ULONG[]> m_highQueueMemory;
    std::unique_ptr<ULONG[]> m_normalQueueMemory;
};

} // namespace dmq::os

#endif // THREADX_DELEGATE_QUEUE_H
