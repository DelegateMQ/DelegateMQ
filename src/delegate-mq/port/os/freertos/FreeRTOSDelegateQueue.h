#ifndef FREERTOS_DELEGATE_QUEUE_H
#define FREERTOS_DELEGATE_QUEUE_H

/// @file FreeRTOSDelegateQueue.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Thin RAII wrapper around the FreeRTOS queue primitive used by
/// dmq::os::Thread to carry pending ThreadMsg pointers.
///
/// @details
/// Isolates the native xQueueCreate/xQueueSend*/xQueueReceive/vQueueDelete
/// calls behind a small type so Thread.cpp's DispatchDelegate()/Run() read as
/// policy and dispatch logic, not FreeRTOS API plumbing. This class owns no
/// policy decisions of its own (FullPolicy, timeout computation, and stats
/// stay in Thread.cpp) -- it only knows how to move ThreadMsg* pointers
/// through a fixed-capacity FreeRTOS queue.

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "port/os/common/ThreadMsg.h"

namespace dmq::os {

class FreeRTOSDelegateQueue
{
public:
    FreeRTOSDelegateQueue() = default;
    ~FreeRTOSDelegateQueue() { Destroy(); }

    FreeRTOSDelegateQueue(const FreeRTOSDelegateQueue&) = delete;
    FreeRTOSDelegateQueue& operator=(const FreeRTOSDelegateQueue&) = delete;

    /// Create the underlying queue with room for 'capacity' pointers.
    /// @return true on success, false if the FreeRTOS queue could not be created (OOM).
    bool Create(size_t capacity)
    {
        if (!m_queue) {
            m_queue = xQueueCreate(static_cast<UBaseType_t>(capacity), sizeof(ThreadMsg*));
        }
        return m_queue != nullptr;
    }

    bool IsCreated() const { return m_queue != nullptr; }

    /// Send a message pointer. 'highPriority' jumps the FIFO (xQueueSendToFront);
    /// otherwise the message goes to the back. 'waitTicks' is 0 for non-blocking,
    /// portMAX_DELAY to block forever, or a finite tick count for a bounded wait.
    /// @return true if the message was enqueued.
    bool Send(ThreadMsg* msg, bool highPriority, TickType_t waitTicks)
    {
        BaseType_t status = highPriority
            ? xQueueSendToFront(m_queue, &msg, waitTicks)
            : xQueueSendToBack(m_queue, &msg, waitTicks);
        return status == pdPASS;
    }

    /// Receive a message pointer, blocking up to 'waitTicks'.
    /// @return the message, or nullptr on timeout.
    ThreadMsg* Receive(TickType_t waitTicks)
    {
        ThreadMsg* msg = nullptr;
        if (xQueueReceive(m_queue, &msg, waitTicks) == pdPASS)
            return msg;
        return nullptr;
    }

    /// Current number of messages waiting in the queue.
    size_t Size() const
    {
        return m_queue ? static_cast<size_t>(uxQueueMessagesWaiting(m_queue)) : 0;
    }

    /// Remove and delete every pending message without blocking. Used during
    /// shutdown once the owning thread has stopped consuming the queue.
    void DrainAndDelete()
    {
        ThreadMsg* msg = nullptr;
        while (xQueueReceive(m_queue, &msg, 0) == pdPASS) {
            delete msg;
        }
    }

    void Destroy()
    {
        if (m_queue) {
            vQueueDelete(m_queue);
            m_queue = nullptr;
        }
    }

private:
    QueueHandle_t m_queue = nullptr;
};

} // namespace dmq::os

#endif // FREERTOS_DELEGATE_QUEUE_H
