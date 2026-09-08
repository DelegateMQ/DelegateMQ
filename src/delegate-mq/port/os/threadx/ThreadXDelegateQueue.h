#ifndef THREADX_DELEGATE_QUEUE_H
#define THREADX_DELEGATE_QUEUE_H

/// @file ThreadXDelegateQueue.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Thin RAII wrapper around the ThreadX TX_QUEUE primitive used by
/// dmq::os::Thread to carry pending ThreadMsg pointers.
///
/// @details
/// Isolates the native tx_queue_create/tx_queue_send/tx_queue_front_send/
/// tx_queue_receive/tx_queue_delete calls (and the ULONG-word buffer sizing
/// they require) behind a small type so Thread.cpp's DispatchDelegate()/Run()
/// read as policy and dispatch logic, not ThreadX API plumbing. This class
/// owns no policy decisions of its own (FullPolicy, timeout computation, and
/// stats stay in Thread.cpp) -- it only knows how to move ThreadMsg*
/// pointers through a fixed-capacity ThreadX queue.

#include <tx_api.h>
#include "port/os/common/ThreadMsg.h"
#include <memory>
#include <new>
#include <cstring>

namespace dmq::os {

class ThreadXDelegateQueue
{
public:
    ThreadXDelegateQueue() { memset(&m_queue, 0, sizeof(m_queue)); }
    ~ThreadXDelegateQueue() { Destroy(); }

    ThreadXDelegateQueue(const ThreadXDelegateQueue&) = delete;
    ThreadXDelegateQueue& operator=(const ThreadXDelegateQueue&) = delete;

    /// Create the underlying queue with room for 'capacity' pointers.
    /// @param name Queue name; ThreadX stores this pointer, not a copy, so
    ///   it must outlive the queue (callers pass THREAD_NAME.c_str(), which
    ///   lives as long as the owning Thread).
    /// @return true on success, false if the buffer allocation or
    ///   tx_queue_create() failed.
    bool Create(size_t capacity, const char* name)
    {
        if (m_queue.tx_queue_id != 0)
            return true;

        // ThreadX queues store "words" (ULONGs). We're passing a pointer
        // (ThreadMsg*), so round up to enough words to hold one.
        UINT msgSizeWords = (sizeof(ThreadMsg*) + sizeof(ULONG) - 1) / sizeof(ULONG);
        ULONG queueMemSizeWords = static_cast<ULONG>(capacity) * msgSizeWords;

        m_queueMemory.reset(new (std::nothrow) ULONG[queueMemSizeWords]);
        if (!m_queueMemory)
            return false;

        UINT ret = tx_queue_create(&m_queue,
                                    const_cast<CHAR*>(name),
                                    msgSizeWords,
                                    m_queueMemory.get(),
                                    queueMemSizeWords * sizeof(ULONG));
        return ret == TX_SUCCESS;
    }

    bool IsCreated() const { return m_queue.tx_queue_id != 0; }

    /// Send a message pointer. 'highPriority' jumps the FIFO
    /// (tx_queue_front_send); otherwise the message goes to the back.
    /// 'waitOption' is TX_NO_WAIT for non-blocking, TX_WAIT_FOREVER to block
    /// forever, or a finite tick count for a bounded wait.
    /// @return true if the message was enqueued.
    bool Send(ThreadMsg* msg, bool highPriority, ULONG waitOption)
    {
        UINT ret = highPriority
            ? tx_queue_front_send(&m_queue, &msg, waitOption)
            : tx_queue_send(&m_queue, &msg, waitOption);
        return ret == TX_SUCCESS;
    }

    /// Receive a message pointer, blocking up to 'waitOption'.
    /// @return the message, or nullptr on timeout.
    ThreadMsg* Receive(ULONG waitOption)
    {
        ThreadMsg* msg = nullptr;
        if (tx_queue_receive(&m_queue, &msg, waitOption) == TX_SUCCESS)
            return msg;
        return nullptr;
    }

    /// Current number of messages waiting in the queue.
    size_t Size()
    {
        if (m_queue.tx_queue_id == 0)
            return 0;

        ULONG enqueued = 0;
        ULONG available = 0;
        TX_THREAD* suspension_list = nullptr;
        ULONG suspension_count = 0;
        TX_QUEUE* next_queue = nullptr;
        if (tx_queue_info_get(&m_queue, nullptr, &enqueued, &available,
                               &suspension_list, &suspension_count, &next_queue) == TX_SUCCESS) {
            return static_cast<size_t>(enqueued);
        }
        return 0;
    }

    /// Remove and delete every pending message without blocking. Used during
    /// shutdown once the owning thread has stopped consuming the queue.
    void DrainAndDelete()
    {
        ThreadMsg* msg = nullptr;
        while (tx_queue_receive(&m_queue, &msg, TX_NO_WAIT) == TX_SUCCESS) {
            delete msg;
        }
    }

    void Destroy()
    {
        if (m_queue.tx_queue_id != 0) {
            tx_queue_delete(&m_queue);
            memset(&m_queue, 0, sizeof(m_queue));
        }
        m_queueMemory.reset();
    }

private:
    TX_QUEUE m_queue;
    std::unique_ptr<ULONG[]> m_queueMemory;
};

} // namespace dmq::os

#endif // THREADX_DELEGATE_QUEUE_H
