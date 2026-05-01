#ifndef _THREAD_FREERTOS_H
#define _THREAD_FREERTOS_H

/// @file Thread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief FreeRTOS implementation of the DelegateMQ IThread interface.
///
/// @details
/// This class provides a concrete implementation of the `IThread` interface using 
/// FreeRTOS primitives (Tasks and Queues). It enables DelegateMQ to dispatch 
/// asynchronous delegates to a dedicated FreeRTOS task.
///
/// @note This implementation is a basic port. For reference, the stdlib and win32
/// implementations provide additional features:
/// 1. Synchronized Startup: CreateThread() blocks until the worker thread is ready.
///
/// **Key Features:**
/// * **Task Integration:** Wraps a FreeRTOS `xTaskCreate` call to establish a
///   dedicated worker loop.
/// * **FullPolicy Support:** Configurable back-pressure (DROP or TIMEOUT) when the
///   message queue is full.
/// * **Priority Support:** Normal and High priorities (uses `xQueueSendToFront`).
/// * **Queue-Based Dispatch:** Uses a FreeRTOS `QueueHandle_t` to receive and
///   process incoming delegate messages in a thread-safe manner.
/// * **Thread Identification:** Implements `GetThreadId()` using `TaskHandle_t`
///   to ensure correct thread context checks (used by `AsyncInvoke` optimizations).
/// * **Graceful Shutdown:** Provides mechanisms (`ExitThread`) to cleanup resources,
///   though typical embedded tasks often run forever.
/// * **Watchdog Integration:** Optional heartbeat mechanism detects stalled or deadlocked
///   threads. Enable by passing a timeout to CreateThread(). Requires
///   Timer::ProcessTimers() to be called from a context that can preempt watched threads
///   — typically a hardware timer ISR or the highest-priority task in the system.

#include "delegate/IThread.h"
#include "extras/util/Timer.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string>
#include <memory>
#include <atomic>
#include <optional>

namespace dmq::os {

class ThreadMsg;

/// @brief Policy applied when the FreeRTOS task queue is full.
/// @details Controls the behavior of DispatchDelegate() when the queue has no space.
///   - DROP:    xQueueSend() with timeout 0 — returns immediately, message discarded.
///   - FAULT:   xQueueSend() with timeout 0 — returns immediately, triggers a system fault if queue full.
///   - TIMEOUT: xQueueSend() with a finite timeout — logs and drops if space is not available in time.
///
/// FAULT is the default. For embedded targets where the caller may be an ISR or
/// high-priority task, consider DROP to avoid blocking at an unsafe context.
enum class FullPolicy { DROP, FAULT, TIMEOUT };

class Thread : public dmq::IThread
{
public:
    /// Default queue size if 0 is passed
    static const size_t DEFAULT_QUEUE_SIZE = 20;

    /// Constructor
    /// @param threadName Name for the FreeRTOS task
    /// @param maxQueueSize Max number of messages in queue (0 = Default 20)
    /// @param fullPolicy Action when queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    Thread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT);

    /// Destructor
    ~Thread();

    /// Called once to create the worker thread. If watchdogTimeout value
    /// provided, the maximum watchdog interval is used. Otherwise no watchdog.
    /// @param[in] watchdogTimeout - optional watchdog timeout.
    /// @return TRUE if thread is created. FALSE otherwise.
    bool CreateThread(std::optional<dmq::Duration> watchdogTimeout = std::nullopt);

    /// Returns true if the thread is created
    bool IsThreadCreated() const { return m_thread != nullptr; }

    /// Terminate the thread gracefully
    void ExitThread();

    /// Get the ID of this thread instance
    TaskHandle_t GetThreadId();

    /// Get the ID of the currently executing thread
    static TaskHandle_t GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    /// Get thread name
    std::string GetThreadName() { return THREAD_NAME; }

    /// Get current queue size
    size_t GetQueueSize();

    /// Sleep for a duration.
    /// @param[in] timeout - the duration to sleep.
    static void Sleep(dmq::Duration timeout);

    /// Set the FreeRTOS Task Priority.
    /// Can be called before or after CreateThread().
    /// @param priority FreeRTOS priority level (0 to configMAX_PRIORITIES-1)
    void SetThreadPriority(int priority);

    /// Optional: Provide a static buffer for the task stack to avoid Heap usage.
    /// @param stackBuffer Pointer to a buffer of type StackType_t. 
    /// @param stackSizeInWords Size of the buffer in WORDS (not bytes).
    void SetStackMem(StackType_t* stackBuffer, uint32_t stackSizeInWords);

    // IThread Interface Implementation
    virtual bool DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg) override;

    /// Update the last alive time for the watchdog. 
    /// @details Normally called automatically by internal timers. For threads with 
    /// blocking loops (e.g. Network receiver), call this manually to prevent timeouts.
    void ThreadCheck();

private:
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    /// Entry point for the thread
    static void Process(void* instance);

    // Run loop called by Process
    void Run();

    /// Check watchdog is expired. Called from Timer::ProcessTimers() context.
    void WatchdogCheck();

    const std::string THREAD_NAME;
    const FullPolicy FULL_POLICY;
    const dmq::Duration m_dispatchTimeout;
    size_t m_queueSize;
    int m_priority;

    TaskHandle_t m_thread = nullptr;
    QueueHandle_t m_queue = nullptr;
    SemaphoreHandle_t m_exitSem = nullptr; // Synchronization for safe destruction
    std::atomic<bool> m_exit = false;

    // Static allocation support
    StackType_t* m_stackBuffer = nullptr;
    uint32_t m_stackSize = 1024; // Default size (words)
    StaticTask_t m_tcb;          // TCB storage for static creation

    // Watchdog related members
    dmq::TimePoint m_lastAliveTime;
    std::unique_ptr<dmq::util::Timer> m_watchdogTimer;
    dmq::ScopedConnection m_watchdogTimerConn;
    std::unique_ptr<dmq::util::Timer> m_threadTimer;
    dmq::ScopedConnection m_threadTimerConn;
    dmq::Duration m_watchdogTimeout;
    dmq::RecursiveMutex m_watchdogMtx;
};

} // namespace dmq::os


#endif
