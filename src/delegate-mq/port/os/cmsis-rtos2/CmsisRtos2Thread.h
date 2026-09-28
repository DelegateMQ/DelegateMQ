#ifndef _THREAD_CMSIS_RTOS2_H
#define _THREAD_CMSIS_RTOS2_H

/// @file CmsisRtos2Thread.h
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
/// * **Priority Support:** Normal and High priorities (uses `osMessageQueuePut`'s
///   native `msg_prio` argument, via `CmsisRtos2DelegateQueue::Send`'s highPriority flag).
/// * **Queue-Based Dispatch:** Uses `CmsisRtos2DelegateQueue` (a thin RAII wrapper
///   around an `osMessageQueue`) to receive and process incoming delegate
///   messages in a thread-safe manner.
/// * **Priority Control:** Supports runtime priority configuration via `SetThreadPriority`
///   using standard `osPriority_t` levels.
/// * **Graceful Shutdown:** Implements robust termination logic using semaphores to ensure
///   the thread exits cleanly before destruction.
/// * **Watchdog Integration:** Optional heartbeat mechanism detects stalled or deadlocked
///   threads. Enable by passing a timeout to CreateThread(). Requires
///   Timer::ProcessTimers() to be called from a context that can preempt watched threads
///   — typically a hardware timer ISR or the highest-priority task in the system.

#include "delegate/IThread.h"
#include "delegate/UnicastDelegate.h"
#include "extras/util/Timer.h"
#include "port/os/common/ThreadMsg.h"
#include "CmsisRtos2DelegateQueue.h"
#include "cmsis_os2.h"
#include <string>
#include <memory>
#include <atomic>
#include <optional>

namespace dmq::os {

/// @brief Policy applied when the thread message queue is full. See dmq::FullPolicy
/// in DelegateOpt.h for the canonical definition, shared by every dmq::os::Thread port.
using FullPolicy = dmq::FullPolicy;

/// @brief What ExitThread() does with queued messages. See dmq::ExitPolicy in DelegateOpt.h.
using ExitPolicy = dmq::ExitPolicy;

class CmsisRtos2Thread : public dmq::IThread
{
public:
#if defined(DMQ_DATABUS_TOOLS)
    /// @brief Statistics captured for thread monitoring.
    struct ThreadStats {
        dmq::xstring cpu_name;
        dmq::xstring thread_name;
        size_t queue_depth;           // Current depth
        size_t queue_depth_max_window;// Max depth since last snapshot
        size_t queue_depth_max_all;   // All-time max depth
        size_t queue_size_limit;      // Max allowed
        float latency_avg_ms;        // Avg wait in window
        float latency_max_window_ms; // Max wait since last snapshot
        float latency_max_all_ms;    // All-time max wait
        float invoke_avg_ms;         // Avg execution in window
        float invoke_max_window_ms;  // Max execution since last snapshot
        float invoke_max_all_ms;     // All-time max execution
        uint64_t dispatch_count;      // Total dispatches (all-time)
    };
#endif

    /// Default queue size if 0 is passed
    static const uint32_t DEFAULT_QUEUE_SIZE = dmq::DEFAULT_QUEUE_SIZE;

    /// Constructor
    /// @param threadName Name for the thread
    /// @param maxQueueSize Max number of messages in queue (0 = Default dmq::DEFAULT_QUEUE_SIZE)
    /// @param fullPolicy Action when queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    /// @param cpuName Optional CPU/Core name grouping for monitoring tools.
    CmsisRtos2Thread(const char* threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const char* cpuName = "");
    
    CmsisRtos2Thread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const std::string& cpuName = "")
        : CmsisRtos2Thread(threadName.c_str(), maxQueueSize, fullPolicy, dispatchTimeout, cpuName.c_str()) {}

    ~CmsisRtos2Thread();

    /// Called once to create the worker thread. If watchdogTimeout value
    /// provided, the maximum watchdog interval is used. Otherwise no watchdog.
    /// @param[in] watchdogTimeout - optional watchdog timeout.
    /// @return TRUE if thread is created. FALSE otherwise.
    bool CreateThread(std::optional<dmq::Duration> watchdogTimeout = std::nullopt);

    /// Shut down the worker thread.
    /// @param[in] policy - DRAIN (default) invokes every message queued before this
    ///   call first; DISCARD invokes only the message already running and cancels
    ///   the rest (see dmq::ExitPolicy). Called from the thread's own message
    ///   handler (a self-exit), queued messages are always discarded.
    void ExitThread(ExitPolicy policy = ExitPolicy::DRAIN);

    osThreadId_t GetThreadId();
    static osThreadId_t GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    /// Set the thread priority.
    /// Can be called before or after CreateThread().
    void SetThreadPriority(osPriority_t priority);

    /// Get current priority
    osPriority_t GetThreadPriority();

    dmq::xstring GetThreadName() { return THREAD_NAME; }

    /// Get current queue size
    size_t GetQueueSize();

    /// Sleep for a duration.
    /// @param[in] timeout - the duration to sleep.
    static void Sleep(dmq::Duration timeout);

    virtual bool DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg) override;

    /// @brief Register a handler invoked when DispatchDelegate() drops a message:
    /// under FullPolicy::DROP (queue full, discarded immediately) or
    /// FullPolicy::TIMEOUT (queue stayed full for dispatchTimeout, discarded).
    /// Optional; unset by default. Called synchronously on the calling (producer)
    /// thread, with the queue depth at the time of the drop.
    void SetDroppedHandler(const dmq::UnicastDelegate<void(size_t)>& handler) { m_droppedHandler = handler; }
    void SetDroppedHandler(dmq::UnicastDelegate<void(size_t)>&& handler) { m_droppedHandler = std::move(handler); }

    /// @brief Register a handler invoked on the worker thread before it processes
    /// any message. Use it for per-thread setup (COM, a language runtime attach,
    /// thread-local state, affinity). CreateThread() returns only after it completes.
    /// @details Must be called while the thread is not running (before CreateThread(),
    /// or after ExitThread()); calling it on a running thread faults. It runs again
    /// on each CreateThread(). Messages dispatched meanwhile are queued, not lost.
    void SetStartHandler(const dmq::UnicastDelegate<void()>& handler) { DMQ_ASSERT_TRUE(m_thread == NULL); m_startHandler = handler; }
    void SetStartHandler(dmq::UnicastDelegate<void()>&& handler) { DMQ_ASSERT_TRUE(m_thread == NULL); m_startHandler = std::move(handler); }

    /// @brief Register a handler invoked on the worker thread after it processes
    /// its last message, as the thread exits. Use it to undo SetStartHandler() setup.
    /// @details Must be called while the thread is not running; calling it on a
    /// running thread faults. The handler is copied when the thread starts.
    /// ExitThread() from another thread returns after it completes. If the thread
    /// exits itself (ExitThread() from a handler on this thread), it runs after the
    /// owning CmsisRtos2Thread may already be destroyed, so it must not touch that object.
    void SetExitHandler(const dmq::UnicastDelegate<void()>& handler) { DMQ_ASSERT_TRUE(m_thread == NULL); m_exitHandler = handler; }
    void SetExitHandler(dmq::UnicastDelegate<void()>&& handler) { DMQ_ASSERT_TRUE(m_thread == NULL); m_exitHandler = std::move(handler); }

    /// @brief Register a handler invoked on the worker thread when its queue has
    /// been empty for `interval`, then again every `interval` while it stays empty.
    /// Any message processed restarts the countdown. Use it for background work that
    /// should yield to messages: polling, housekeeping, entering low power.
    /// @details Must be called while the thread is not running; calling it on a
    /// running thread faults. The handler and interval are copied when the thread
    /// starts. Not called once ExitThread() has been requested.
    /// @param[in] handler - the idle handler, or an empty delegate to disable.
    /// @param[in] interval - quiet time before each call. Must be greater than zero.
    ///                       Defaults to dmq::THREAD_IDLE_INTERVAL (DMQ_THREAD_IDLE_INTERVAL).
    void SetIdleHandler(const dmq::UnicastDelegate<void()>& handler, dmq::Duration interval = dmq::THREAD_IDLE_INTERVAL)
    {
        DMQ_ASSERT_TRUE(m_thread == NULL);
        DMQ_ASSERT_TRUE(interval > dmq::Duration::zero());
        m_idleHandler = handler;
        m_idleInterval = interval;
    }
    void SetIdleHandler(dmq::UnicastDelegate<void()>&& handler, dmq::Duration interval = dmq::THREAD_IDLE_INTERVAL)
    {
        DMQ_ASSERT_TRUE(m_thread == NULL);
        DMQ_ASSERT_TRUE(interval > dmq::Duration::zero());
        m_idleHandler = std::move(handler);
        m_idleInterval = interval;
    }

    /// @brief Manually update the watchdog alive timestamp.
    /// @details The Run() loop refreshes the timestamp automatically on every iteration.
    /// Call this from inside long-running message handlers to prevent a false watchdog
    /// alarm when a handler legitimately takes longer than watchdogTimeout.
    void ThreadCheck();

    /// @brief Static method to check all registered threads for watchdog expiration.
    static void WatchdogCheckAll();

#if defined(DMQ_DATABUS_TOOLS)
    /// @brief Capture and reset windowed statistics.
    ThreadStats SnapshotStats();
#endif

private:
    CmsisRtos2Thread(const CmsisRtos2Thread&) = delete;
    CmsisRtos2Thread& operator=(const CmsisRtos2Thread&) = delete;

    // Entry point
    static void Process(void* argument);
    void Run();

    /// Check watchdog is expired. Called from Timer::ProcessTimers() context.
    void WatchdogCheck();

    /// Get registry head using the "Immortal" Pattern
    static CmsisRtos2Thread*& GetWatchdogHead();

    /// Get registry lock using the "Immortal" Pattern
    static dmq::RecursiveMutex& GetWatchdogLock();

    const dmq::xstring THREAD_NAME;
    const dmq::xstring CPU_NAME;
    const size_t m_queueSize;
    const FullPolicy FULL_POLICY;
    const dmq::Duration m_dispatchTimeout;
    osPriority_t m_priority;

    osThreadId_t m_thread = NULL;
    CmsisRtos2DelegateQueue m_queue;

    // Optional handler invoked when a message is dropped (FullPolicy::DROP or TIMEOUT)
    dmq::UnicastDelegate<void(size_t)> m_droppedHandler;

    // Optional handlers invoked on the worker thread at start, exit and when idle
    dmq::UnicastDelegate<void()> m_startHandler;
    dmq::UnicastDelegate<void()> m_exitHandler;
    dmq::UnicastDelegate<void()> m_idleHandler;
    dmq::Duration m_idleInterval = dmq::Duration::zero();
    osSemaphoreId_t m_exitSem = NULL; // Semaphore to signal thread completion
    osSemaphoreId_t m_startSem = NULL; // Released by Run() once the start handler has run
    bool m_startSync = false; // CreateThread() waits for the start handler (kernel running)
    std::atomic<bool> m_exit = false;
    std::atomic<bool> m_discard = false; // ExitPolicy::DISCARD: cancel queued messages instead of invoking
    bool* m_selfExitPtr = nullptr;
    
    // Configurable sizes
    static const uint32_t STACK_SIZE = 2048; // Bytes
    static const uint32_t START_FLAG = 0x1U; // Thread flag: CreateThread() has stored m_thread

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::atomic<dmq::Duration> m_watchdogTimeout;
    CmsisRtos2Thread* m_watchdogNext = nullptr;

#if defined(DMQ_DATABUS_TOOLS)
    osMutexId_t m_statMutex = NULL; // Mutex to protect statistics
    // Monitoring statistics members
    size_t m_queueDepthMaxWindow = 0;
    size_t m_queueDepthMaxAll = 0;

    dmq::Duration m_latencyTotalWindow = dmq::Duration(0);
    uint32_t m_latencyCountWindow = 0;
    dmq::Duration m_latencyMaxWindow = dmq::Duration(0);
    dmq::Duration m_latencyMaxAll = dmq::Duration(0);

    dmq::Duration m_invokeTotalWindow = dmq::Duration(0);
    uint32_t m_invokeCountWindow = 0;
    dmq::Duration m_invokeMaxWindow = dmq::Duration(0);
    dmq::Duration m_invokeMaxAll = dmq::Duration(0);

    uint64_t m_dispatchCountAll = 0;
#endif
};

/// @brief Backward-compatible name: existing code referencing dmq::os::Thread
/// keeps compiling unchanged against the CMSIS-RTOS2 port.
using Thread = CmsisRtos2Thread;

} // namespace dmq::os

#endif // _THREAD_CMSIS_RTOS2_H
