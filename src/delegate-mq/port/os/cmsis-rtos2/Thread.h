#ifndef _THREAD_CMSIS_RTOS2_H
#define _THREAD_CMSIS_RTOS2_H

/// @file Thread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief CMSIS-RTOS2 implementation of the DelegateMQ IThread interface.
///
/// @details
/// This class provides a concrete implementation of the `IThread` interface using 
/// the CMSIS-RTOS2 standard API (`cmsis_os2.h`). It enables DelegateMQ to dispatch 
/// asynchronous delegates to a dedicated thread on any CMSIS-compliant RTOS 
/// (e.g., Keil RTX, FreeRTOS wrapped by CMSIS, Zephyr, etc.).
///
/// @note This implementation is a basic port. For reference, the stdlib and win32
/// implementations provide additional features:
/// 1. Synchronized Startup: CreateThread() blocks until the worker thread is ready.
///
/// **Key Features:**
/// * **Task Integration:** Wraps `osThreadNew` to establish a dedicated worker loop.
/// * **FullPolicy Support:** Configurable back-pressure (DROP or TIMEOUT) when the
///   message queue is full.
/// * **Priority Support:** Normal and High priorities (uses `msg_prio`).
/// * **Queue-Based Dispatch:** Uses `osMessageQueue` to receive and process incoming
///   delegate messages in a thread-safe manner.
/// * **Priority Control:** Supports runtime priority configuration via `SetThreadPriority`
///   using standard `osPriority_t` levels.
/// * **Graceful Shutdown:** Implements robust termination logic using semaphores to ensure
///   the thread exits cleanly before destruction.
/// * **Watchdog Integration:** Optional heartbeat mechanism detects stalled or deadlocked
///   threads. Enable by passing a timeout to CreateThread(). Requires
///   Timer::ProcessTimers() to be called from a context that can preempt watched threads
///   — typically a hardware timer ISR or the highest-priority task in the system.

#include "delegate/IThread.h"
#include "extras/util/Timer.h"
#include "cmsis_os2.h"
#include <string>
#include <memory>
#include <atomic>
#include <optional>

namespace dmq::os {

class ThreadMsg;

/// @brief Policy applied when the thread message queue is full.
/// @details Only meaningful when maxQueueSize > 0.
///   - DROP:    DispatchDelegate() silently discards the message and returns immediately.
///   - FAULT:   DispatchDelegate() triggers a system fault if the queue is full.
///   - TIMEOUT: DispatchDelegate() waits up to dispatchTimeout, then logs and drops.
///
/// Use DROP for high-rate best-effort topics (sensor telemetry, display updates) where
/// a stale sample is preferable to stalling the publisher. FAULT is the default.
enum class FullPolicy { DROP, FAULT, TIMEOUT };

class Thread : public dmq::IThread
{
public:
    /// Default queue size if 0 is passed
    static const uint32_t DEFAULT_QUEUE_SIZE = 20;

    /// Constructor
    /// @param threadName Name for the thread
    /// @param maxQueueSize Max number of messages in queue (0 = Default 20)
    /// @param fullPolicy Action when queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    Thread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT);
    
    ~Thread();

    /// Called once to create the worker thread. If watchdogTimeout value
    /// provided, the maximum watchdog interval is used. Otherwise no watchdog.
    /// @param[in] watchdogTimeout - optional watchdog timeout.
    /// @return TRUE if thread is created. FALSE otherwise.
    bool CreateThread(std::optional<dmq::Duration> watchdogTimeout = std::nullopt);
    void ExitThread();

    osThreadId_t GetThreadId();
    static osThreadId_t GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    /// Set the thread priority.
    /// Can be called before or after CreateThread().
    void SetThreadPriority(osPriority_t priority);

    /// Get current priority
    osPriority_t GetThreadPriority();

    std::string GetThreadName() { return THREAD_NAME; }

    /// Get current queue size
    size_t GetQueueSize();

    /// Sleep for a duration.
    /// @param[in] timeout - the duration to sleep.
    static void Sleep(dmq::Duration timeout);

    virtual bool DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg) override;

private:
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    // Entry point
    static void Process(void* argument);
    void Run();

    /// Check watchdog is expired. Called from Timer::ProcessTimers() context.
    void WatchdogCheck();

    /// @brief Manually update the watchdog alive timestamp.
    /// @details The Run() loop refreshes the timestamp automatically on every iteration.
    /// Call this from inside long-running message handlers to prevent a false watchdog
    /// alarm when a handler legitimately takes longer than watchdogTimeout.
    void ThreadCheck();

    const std::string THREAD_NAME;
    const size_t m_queueSize;
    const FullPolicy FULL_POLICY;
    const dmq::Duration m_dispatchTimeout;
    osPriority_t m_priority;

    osThreadId_t m_thread = NULL;
    osMessageQueueId_t m_msgq = NULL;
    osSemaphoreId_t m_exitSem = NULL; // Semaphore to signal thread completion
    std::atomic<bool> m_exit = false;
    
    // Configurable sizes
    static const uint32_t STACK_SIZE = 2048; // Bytes

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::unique_ptr<dmq::util::Timer> m_watchdogTimer;
    dmq::ScopedConnection m_watchdogTimerConn;
    std::atomic<dmq::Duration> m_watchdogTimeout;
};

} // namespace dmq::os

#endif // _THREAD_CMSIS_RTOS2_H