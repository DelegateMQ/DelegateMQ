#ifndef CMSIS_RTOS2_DELEGATE_QUEUE_H
#define CMSIS_RTOS2_DELEGATE_QUEUE_H

/// @file CmsisRtos2DelegateQueue.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Thin RAII wrapper around a pair of CMSIS-RTOS2 osMessageQueue
/// primitives used by dmq::os::Thread to carry pending ThreadMsg pointers,
/// with priority support. See the Priority enum in delegate/DelegateMsg.h for
/// the cross-port ordering contract this class must uphold.
///
/// @details
/// *** DO NOT "SIMPLIFY" THIS BACK TO ONE QUEUE + msg_prio ***
/// osMessageQueuePut() takes a msg_prio argument, and an earlier version of
/// this class relied on it for HIGH priority with a single queue. CMSIS-RTOS2
/// leaves msg_prio's effect to the implementation, and common ones ignore it:
/// Zephyr's CMSIS-RTOS2 layer (the backend this port is verified on) does
/// ARG_UNUSED(msg_prio) and always appends with k_msgq_put(), so HIGH messages
/// were delivered behind every NORMAL one already queued, breaking the Priority
/// contract every other port meets.
///
/// The fix is the same design as FreeRTOSDelegateQueue and ThreadXDelegateQueue:
/// two independent osMessageQueue instances, one per priority lane, both always
/// appended to, and a counting osSemaphore as a shared "doorbell". Every
/// successful Send() (to either lane) releases the semaphore once; Receive()
/// acquires it (the single blocking wait) and then does a non-blocking get,
/// checking the HIGH lane first so it drains ahead of NORMAL while each lane
/// stays FIFO.
///
/// Each lane is sized to the full requested 'capacity', so worst-case combined
/// depth is 2x capacity; the semaphore's max count is sized to match (2x
/// capacity) so a Send() that already found room in its lane can never fail to
/// release it.
///
/// Send() is callable from an ISR with a zero timeout (osMessageQueuePut and
/// osSemaphoreRelease are ISR-safe in CMSIS-RTOS2); Receive() is only called by
/// the owning thread. This class owns no policy decisions of its own
/// (FullPolicy, timeout computation, and stats stay in Thread.cpp) -- it only
/// knows how to move ThreadMsg* pointers through two fixed-capacity CMSIS-RTOS2
/// queues in priority order.

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

    /// Create both lanes (each with room for 'capacity' pointers) and the
    /// doorbell semaphore. On a partial failure everything created so far is
    /// destroyed, so a later Create() starts clean.
    /// @return true on success, false if any CMSIS-RTOS2 object couldn't be created.
    bool Create(size_t capacity)
    {
        if (IsCreated())
            return true;

        const uint32_t cap = static_cast<uint32_t>(capacity);
        if (!m_highQueue)
            m_highQueue = osMessageQueueNew(cap, sizeof(ThreadMsg*), NULL);
        if (!m_normalQueue)
            m_normalQueue = osMessageQueueNew(cap, sizeof(ThreadMsg*), NULL);
        if (!m_countSem)
            m_countSem = osSemaphoreNew(cap * 2, 0, NULL);

        if (!IsCreated()) {
            Destroy();
            return false;
        }
        return true;
    }

    bool IsCreated() const { return m_highQueue != NULL && m_normalQueue != NULL && m_countSem != NULL; }

    /// Send a message pointer. 'highPriority' routes to the HIGH lane, which
    /// Receive() always drains first; otherwise the NORMAL lane. Both lanes are
    /// always appended to. 'waitOption' is 0 for non-blocking, osWaitForever to
    /// block forever, or a finite timeout in kernel ticks (see CmsisRtos2ToTicks());
    /// it applies only to waiting for room in the lane.
    /// @return true if the message was enqueued.
    bool Send(ThreadMsg* msg, bool highPriority, uint32_t waitOption)
    {
        osMessageQueueId_t q = highPriority ? m_highQueue : m_normalQueue;
        if (osMessageQueuePut(q, &msg, 0, waitOption) != osOK)
            return false;
        osSemaphoreRelease(m_countSem);
        return true;
    }

    /// Receive a message pointer, HIGH lane first, blocking up to 'waitOption'
    /// for either lane to have one.
    /// @return the message, or nullptr on timeout.
    ThreadMsg* Receive(uint32_t waitOption)
    {
        if (osSemaphoreAcquire(m_countSem, waitOption) != osOK)
            return nullptr; // timed out waiting for either lane

        ThreadMsg* msg = nullptr;
        if (osMessageQueueGet(m_highQueue, &msg, NULL, 0) == osOK)
            return msg;
        if (osMessageQueueGet(m_normalQueue, &msg, NULL, 0) == osOK)
            return msg;
        return nullptr;
    }

    /// Current number of messages waiting in both lanes.
    size_t Size() const
    {
        if (!m_highQueue || !m_normalQueue)
            return 0;
        return static_cast<size_t>(osMessageQueueGetCount(m_highQueue))
             + static_cast<size_t>(osMessageQueueGetCount(m_normalQueue));
    }

    /// Remove and delete every pending message without blocking. Used during
    /// shutdown once the owning thread has stopped consuming the queue.
    /// Each message is canceled first (CancelThreadMsg()), releasing a sender
    /// blocked waiting on it.
    void DrainAndDelete()
    {
        ThreadMsg* msg = nullptr;
        if (m_highQueue) {
            while (osMessageQueueGet(m_highQueue, &msg, NULL, 0) == osOK) {
                CancelThreadMsg(*msg);  // Release a sender blocked on it
                delete msg;
            }
        }
        if (m_normalQueue) {
            while (osMessageQueueGet(m_normalQueue, &msg, NULL, 0) == osOK) {
                CancelThreadMsg(*msg);  // Release a sender blocked on it
                delete msg;
            }
        }
    }

    void Destroy()
    {
        if (m_highQueue) {
            osMessageQueueDelete(m_highQueue);
            m_highQueue = NULL;
        }
        if (m_normalQueue) {
            osMessageQueueDelete(m_normalQueue);
            m_normalQueue = NULL;
        }
        if (m_countSem) {
            osSemaphoreDelete(m_countSem);
            m_countSem = NULL;
        }
    }

private:
    osMessageQueueId_t m_highQueue = NULL;
    osMessageQueueId_t m_normalQueue = NULL;
    osSemaphoreId_t m_countSem = NULL;
};

} // namespace dmq::os

#endif // CMSIS_RTOS2_DELEGATE_QUEUE_H
