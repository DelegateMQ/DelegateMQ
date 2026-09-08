#ifndef CMSIS_RTOS2_DELEGATE_QUEUE_H
#define CMSIS_RTOS2_DELEGATE_QUEUE_H

/// @file CmsisRtos2DelegateQueue.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Thin RAII wrapper around the CMSIS-RTOS2 osMessageQueue primitive
/// used by dmq::os::Thread to carry pending ThreadMsg pointers.
///
/// @details
/// Isolates the native osMessageQueueNew/osMessageQueuePut/osMessageQueueGet/
/// osMessageQueueDelete calls behind a small type so Thread.cpp's
/// DispatchDelegate()/Run() read as policy and dispatch logic, not CMSIS-RTOS2
/// API plumbing. Unlike FreeRTOS/ThreadX (front-of-queue send) or Zephyr (no
/// native priority at all), CMSIS-RTOS2's osMessageQueuePut takes a msg_prio
/// argument directly, so a single osMessageQueueId_t handles both priority
/// lanes -- no second queue needed here. This class owns no policy decisions
/// of its own (FullPolicy, timeout computation, and stats stay in
/// Thread.cpp) -- it only knows how to move ThreadMsg* pointers through a
/// fixed-capacity CMSIS-RTOS2 queue.

#include "cmsis_os2.h"
#include "port/os/common/ThreadMsg.h"

namespace dmq::os {

class CmsisRtos2DelegateQueue
{
public:
    CmsisRtos2DelegateQueue() = default;
    ~CmsisRtos2DelegateQueue() { Destroy(); }

    CmsisRtos2DelegateQueue(const CmsisRtos2DelegateQueue&) = delete;
    CmsisRtos2DelegateQueue& operator=(const CmsisRtos2DelegateQueue&) = delete;

    /// Create the underlying queue with room for 'capacity' pointers.
    /// @return true on success, false if osMessageQueueNew() failed.
    bool Create(size_t capacity)
    {
        if (!m_msgq) {
            m_msgq = osMessageQueueNew(static_cast<uint32_t>(capacity), sizeof(ThreadMsg*), NULL);
        }
        return m_msgq != NULL;
    }

    bool IsCreated() const { return m_msgq != NULL; }

    /// Send a message pointer. 'highPriority' is passed as CMSIS-RTOS2's
    /// native msg_prio argument -- a higher-priority message is delivered
    /// ahead of lower-priority ones already queued, no second queue needed.
    /// 'waitOption' is 0 for non-blocking, osWaitForever to block forever, or
    /// a finite millisecond timeout for a bounded wait.
    /// @return true if the message was enqueued.
    bool Send(ThreadMsg* msg, bool highPriority, uint32_t waitOption)
    {
        uint8_t msg_prio = highPriority ? 1 : 0;
        return osMessageQueuePut(m_msgq, &msg, msg_prio, waitOption) == osOK;
    }

    /// Receive a message pointer, blocking up to 'waitOption'.
    /// @return the message, or nullptr on timeout.
    ThreadMsg* Receive(uint32_t waitOption)
    {
        ThreadMsg* msg = nullptr;
        if (osMessageQueueGet(m_msgq, &msg, NULL, waitOption) == osOK)
            return msg;
        return nullptr;
    }

    /// Current number of messages waiting in the queue.
    size_t Size() const
    {
        return m_msgq ? static_cast<size_t>(osMessageQueueGetCount(m_msgq)) : 0;
    }

    /// Remove and delete every pending message without blocking. Used during
    /// shutdown once the owning thread has stopped consuming the queue.
    void DrainAndDelete()
    {
        ThreadMsg* msg = nullptr;
        while (osMessageQueueGet(m_msgq, &msg, NULL, 0) == osOK) {
            delete msg;
        }
    }

    void Destroy()
    {
        if (m_msgq) {
            osMessageQueueDelete(m_msgq);
            m_msgq = NULL;
        }
    }

private:
    osMessageQueueId_t m_msgq = NULL;
};

} // namespace dmq::os

#endif // CMSIS_RTOS2_DELEGATE_QUEUE_H
