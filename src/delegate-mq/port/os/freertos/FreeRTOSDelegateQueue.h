#ifndef FREERTOS_DELEGATE_QUEUE_H
#define FREERTOS_DELEGATE_QUEUE_H

/// @file FreeRTOSDelegateQueue.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Thin RAII wrapper around a pair of FreeRTOS xQueue primitives used
/// by dmq::os::Thread to carry pending ThreadMsg pointers, with priority
/// support. See the Priority enum in delegate/DelegateMsg.h for the
/// cross-port ordering contract this class must uphold.
///
/// @details
/// *** DO NOT "SIMPLIFY" THIS BACK TO ONE QUEUE + xQueueSendToFront() ***
/// An earlier version of this class used a single xQueue and called
/// xQueueSendToFront() for HIGH priority messages. That is WRONG: sending to
/// the front of a single queue makes HIGH messages a LIFO stack relative to
/// each other, not a FIFO lane. Concretely, starting from [Normal1]:
///   1. HIGH-A sent to front  -> [HIGH-A, Normal1]
///   2. HIGH-B sent to front  -> [HIGH-B, HIGH-A, Normal1]
/// The consumer then dequeues HIGH-B before HIGH-A, even though A arrived
/// first -- exactly backwards. See DelegateMsg.h's Priority comment for the
/// required contract (HIGH jumps ahead of NORMAL, but FIFO is preserved
/// within each priority).
///
/// The fix: two independent xQueue instances, one per priority lane, each
/// always pushed to the BACK (xQueueSendToBack, never xQueueSendToFront).
/// FreeRTOS gives no way to block on "either of two queues has data" the way
/// a std::condition_variable can wait on an arbitrary predicate, so a
/// counting semaphore is used as a shared "doorbell": every successful Send()
/// (to either lane) gives the semaphore once; Receive() takes the semaphore
/// (the single blocking wait) and then does a non-blocking pop, checking the
/// HIGH lane first so it still drains ahead of NORMAL. This mirrors
/// StdlibThread's two-std::deque + one-condition_variable design and
/// ZephyrDelegateQueue's two-k_msgq + k_poll design -- same shape, RTOS-native
/// primitives.
///
/// Each lane is sized to the full requested 'capacity' (like
/// ZephyrDelegateQueue), so worst-case combined depth is 2x capacity rather
/// than exactly capacity -- this doubles queue memory relative to the old
/// single-queue design. The semaphore's max count is sized to match (2x
/// capacity) so a Send() that already found room in its lane can never fail
/// to give the semaphore.
///
/// Isolates the native xQueueCreate/xQueueSendToBack/xQueueReceive/
/// vQueueDelete/xSemaphoreCreateCounting calls behind a small type so
/// Thread.cpp's DispatchDelegate()/Run() read as policy and dispatch logic,
/// not FreeRTOS API plumbing. This class owns no policy decisions of its own
/// (FullPolicy, timeout computation, and stats stay in Thread.cpp) -- it only
/// knows how to move ThreadMsg* pointers through two fixed-capacity FreeRTOS
/// queues in priority order.
///
/// @note Requires configUSE_COUNTING_SEMAPHORES == 1 in the application's
/// FreeRTOSConfig.h (needed for xSemaphoreCreateCounting).

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "port/os/common/ThreadMsg.h"

namespace dmq::os {

class FreeRTOSDelegateQueue
{
public:
    FreeRTOSDelegateQueue() = default;
    ~FreeRTOSDelegateQueue() { Destroy(); }

    FreeRTOSDelegateQueue(const FreeRTOSDelegateQueue&) = delete;
    FreeRTOSDelegateQueue& operator=(const FreeRTOSDelegateQueue&) = delete;

    /// Create the underlying HIGH and NORMAL queues, each with room for
    /// 'capacity' pointers, plus the counting semaphore doorbell (max count
    /// 2 * capacity, matching the combined capacity of both lanes).
    /// @return true on success, false if any FreeRTOS object could not be
    /// created (OOM).
    bool Create(size_t capacity)
    {
        if (m_highQueue && m_normalQueue && m_countSem)
            return true;

        if (!m_highQueue)
            m_highQueue = xQueueCreate(static_cast<UBaseType_t>(capacity), sizeof(ThreadMsg*));
        if (!m_normalQueue)
            m_normalQueue = xQueueCreate(static_cast<UBaseType_t>(capacity), sizeof(ThreadMsg*));
        if (!m_countSem)
            m_countSem = xSemaphoreCreateCounting(static_cast<UBaseType_t>(capacity * 2), 0);

        if (!m_highQueue || !m_normalQueue || !m_countSem) {
            Destroy();
            return false;
        }
        return true;
    }

    bool IsCreated() const { return m_highQueue != nullptr && m_normalQueue != nullptr && m_countSem != nullptr; }

    /// Send a message pointer. 'highPriority' routes to the HIGH lane, which
    /// Receive() always drains first; otherwise the NORMAL lane. Both lanes
    /// are always pushed to the BACK -- see the file-level comment for why
    /// "send to front" must never be reintroduced here. 'waitTicks' is 0 for
    /// non-blocking, portMAX_DELAY to block forever, or a finite tick count
    /// for a bounded wait; it applies only to the queue push (each lane has
    /// its own independent capacity -- there is no combined-depth wait across
    /// both). Giving the doorbell semaphore never blocks and cannot fail: its
    /// max count (2 * capacity) matches the combined lane capacity, so a
    /// Send() that already found room in its lane always has room in the
    /// semaphore's count too.
    /// @return true if the message was enqueued.
    bool Send(ThreadMsg* msg, bool highPriority, TickType_t waitTicks)
    {
        QueueHandle_t q = highPriority ? m_highQueue : m_normalQueue;
        if (xQueueSendToBack(q, &msg, waitTicks) != pdPASS)
            return false;
        xSemaphoreGive(m_countSem);
        return true;
    }

    /// Receive a message pointer, blocking up to 'waitTicks'. The HIGH lane
    /// is always checked -- and drained -- first, so a pending HIGH message
    /// is returned even if NORMAL messages arrived earlier. Blocks on the
    /// doorbell semaphore (the single native wait), then does a
    /// non-blocking pop from whichever lane has data; the semaphore count
    /// guarantees at least one lane is non-empty once the take succeeds.
    /// @return the message, or nullptr on timeout.
    ThreadMsg* Receive(TickType_t waitTicks)
    {
        if (xSemaphoreTake(m_countSem, waitTicks) != pdPASS)
            return nullptr; // timed out waiting for either lane

        ThreadMsg* msg = nullptr;
        if (xQueueReceive(m_highQueue, &msg, 0) == pdPASS)
            return msg;
        if (xQueueReceive(m_normalQueue, &msg, 0) == pdPASS)
            return msg;
        return nullptr;
    }

    /// Combined number of messages waiting across both lanes.
    size_t Size() const
    {
        if (!m_highQueue || !m_normalQueue)
            return 0;
        return static_cast<size_t>(uxQueueMessagesWaiting(m_highQueue))
             + static_cast<size_t>(uxQueueMessagesWaiting(m_normalQueue));
    }

    /// Remove and delete every pending message in both lanes without
    /// blocking. Used during shutdown once the owning thread has stopped
    /// consuming the queues.
    void DrainAndDelete()
    {
        ThreadMsg* msg = nullptr;
        while (xQueueReceive(m_highQueue, &msg, 0) == pdPASS) {
            delete msg;
        }
        while (xQueueReceive(m_normalQueue, &msg, 0) == pdPASS) {
            delete msg;
        }
    }

    void Destroy()
    {
        if (m_highQueue) {
            vQueueDelete(m_highQueue);
            m_highQueue = nullptr;
        }
        if (m_normalQueue) {
            vQueueDelete(m_normalQueue);
            m_normalQueue = nullptr;
        }
        if (m_countSem) {
            vSemaphoreDelete(m_countSem);
            m_countSem = nullptr;
        }
    }

private:
    QueueHandle_t m_highQueue = nullptr;
    QueueHandle_t m_normalQueue = nullptr;
    SemaphoreHandle_t m_countSem = nullptr;
};

} // namespace dmq::os

#endif // FREERTOS_DELEGATE_QUEUE_H
