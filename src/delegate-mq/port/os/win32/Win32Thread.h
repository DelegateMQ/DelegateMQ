#ifndef _THREAD_WIN32_H
#define _THREAD_WIN32_H

/// @file Win32Thread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Win32 API implementation of the DelegateMQ IThread interface.
///
/// @details
/// This class provides a Windows-native implementation of the `IThread` interface using
/// Win32 synchronization primitives (`CRITICAL_SECTION`, `CONDITION_VARIABLE`, `HANDLE`).
/// It creates a dedicated worker thread with an event loop capable of processing
/// asynchronous delegates and system messages.
///
/// **Key Features:**
/// * **Priority Queue:** Uses `std::priority_queue` to ensure high-priority delegate
///   messages (e.g., system signals) are processed before lower-priority ones.
/// * **Queue Full Policy:** Configurable `FullPolicy` (DROP, FAULT, or TIMEOUT), always enforced --
///   `maxQueueSize == 0` falls back to `dmq::THREAD_DESKTOP_QUEUE_SIZE` rather than disabling
///   the cap, since this port backs its queue with a plain `std::deque` and would otherwise
///   grow without bound if the destination thread is dead/stuck. TIMEOUT waits up to
///   `dispatchTimeout` for the consumer before logging and dropping; DROP silently discards
///   immediately. FAULT (the default) triggers a system fault.
/// * **Watchdog Integration:** Includes a built-in heartbeat mechanism. If the thread loop
///   stalls (deadlock or infinite loop), the watchdog timer detects the failure.
/// * **Synchronized Start:** Uses a Win32 manual-reset event to ensure the thread
///   is fully initialized and running before `CreateThread()` returns.
/// * **Debug Support:** Sets the native thread name via `SetThreadDescription()` to
///   aid debugging in Visual Studio.

#include "delegate/IThread.h"
#include "delegate/UnicastDelegate.h"
#include "./extras/util/Timer.h"
#include "port/os/common/ThreadMsg.h"
#include <deque>
#include <atomic>
#include <optional>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace dmq::os {

/// @brief Policy applied when the thread message queue is full. See dmq::FullPolicy
/// in DelegateOpt.h for the canonical definition, shared by every dmq::os::Thread port.
using FullPolicy = dmq::FullPolicy;

/// @brief What ExitThread() does with queued messages. See dmq::ExitPolicy in DelegateOpt.h.
using ExitPolicy = dmq::ExitPolicy;

/// @brief Windows-native thread for systems using the Win32 API.
/// @details The Win32Thread class creates a worker thread capable of dispatching and
/// invoking asynchronous delegates.
class Win32Thread : public dmq::IThread
{
    XALLOCATOR
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

    /// Constructor
    /// @param threadName The name of the thread for debugging.
    /// @param maxQueueSize The maximum number of messages allowed in the queue.
    ///                     0 falls back to dmq::THREAD_DESKTOP_QUEUE_SIZE -- a high-water-mark
    ///                     safety net, not a throughput limiter, against unbounded growth if
    ///                     the destination thread is dead/stuck.
    /// @param fullPolicy When the queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    /// @param cpuName Optional CPU/Core name grouping for monitoring tools.
    Win32Thread(const char* threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const char* cpuName = "");

    Win32Thread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const std::string& cpuName = "")
        : Win32Thread(threadName.c_str(), maxQueueSize, fullPolicy, dispatchTimeout, cpuName.c_str()) {}

    /// Destructor
    virtual ~Win32Thread();

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

    /// Get the ID of this thread instance
    DWORD GetThreadId();

    /// Get the ID of the currently executing thread
    static DWORD GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    /// Get thread name
    dmq::xstring GetThreadName() { return THREAD_NAME; }

    /// Get size of thread message queue.
    size_t GetQueueSize();

    /// Sleep for a duration.
    /// @param[in] timeout - the duration to sleep.
    static void Sleep(dmq::Duration timeout);

    /// Dispatch and invoke a delegate target on the destination thread.
    /// @param[in] msg - Delegate message containing target function
    /// arguments.
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
    void SetStartHandler(const dmq::UnicastDelegate<void()>& handler) { DMQ_ASSERT_TRUE(m_hThread == NULL); m_startHandler = handler; }
    void SetStartHandler(dmq::UnicastDelegate<void()>&& handler) { DMQ_ASSERT_TRUE(m_hThread == NULL); m_startHandler = std::move(handler); }

    /// @brief Register a handler invoked on the worker thread after it processes
    /// its last message, as the thread exits. Use it to undo SetStartHandler() setup.
    /// @details Must be called while the thread is not running; calling it on a
    /// running thread faults. The handler is copied when the thread starts.
    /// ExitThread() from another thread returns after it completes. If the thread
    /// exits itself (ExitThread() from a handler on this thread), it runs after the
    /// owning Win32Thread may already be destroyed, so it must not touch that object.
    void SetExitHandler(const dmq::UnicastDelegate<void()>& handler) { DMQ_ASSERT_TRUE(m_hThread == NULL); m_exitHandler = handler; }
    void SetExitHandler(dmq::UnicastDelegate<void()>&& handler) { DMQ_ASSERT_TRUE(m_hThread == NULL); m_exitHandler = std::move(handler); }

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
        DMQ_ASSERT_TRUE(m_hThread == NULL);
        DMQ_ASSERT_TRUE(interval > dmq::Duration::zero());
        m_idleHandler = handler;
        m_idleInterval = interval;
    }
    void SetIdleHandler(dmq::UnicastDelegate<void()>&& handler, dmq::Duration interval = dmq::THREAD_IDLE_INTERVAL)
    {
        DMQ_ASSERT_TRUE(m_hThread == NULL);
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
    Win32Thread(const Win32Thread&) = delete;
    Win32Thread& operator=(const Win32Thread&) = delete;

    /// Win32 thread proc entry point
    static DWORD WINAPI ThreadProc(LPVOID lpParam);

    /// Entry point for the thread
    void Process();

    /// Check watchdog is expired. This function is called by the thread
    /// that calls dmq::util::Timer::ProcessTimers(). This function is thread-safe.
    /// In a real-time OS, dmq::util::Timer::ProcessTimers() typically is called by the highest
    /// priority task in the system.
    void WatchdogCheck();

    /// Get registry head using the "Immortal" Pattern
    static Win32Thread*& GetWatchdogHead();

    /// Get registry lock using the "Immortal" Pattern
    static dmq::RecursiveMutex& GetWatchdogLock();

    HANDLE m_hThread = NULL;
    DWORD m_threadId = 0;

    // Manual-reset event to synchronize thread startup
    HANDLE m_hStartEvent = NULL;

    CRITICAL_SECTION m_cs;

    // Condition variable to wake up consumers when a message is enqueued
    CONDITION_VARIABLE m_cvNotEmpty;

    // Condition variable to wake up blocked producers when space is available
    CONDITION_VARIABLE m_cvNotFull;

    std::deque<std::shared_ptr<ThreadMsg>> m_highQueue;
    std::deque<std::shared_ptr<ThreadMsg>> m_normalQueue;

    const dmq::xstring THREAD_NAME;
    const dmq::xstring CPU_NAME;

    // Max queue size for back pressure (0 = unlimited)
    const size_t MAX_QUEUE_SIZE;

    // Policy applied when the thread message queue is full.
    const FullPolicy FULL_POLICY;

    // Timeout duration for TIMEOUT policy
    const dmq::Duration m_dispatchTimeout;

    // Optional handler invoked when a message is dropped (FullPolicy::DROP or TIMEOUT)
    dmq::UnicastDelegate<void(size_t)> m_droppedHandler;

    // Optional handlers invoked on the worker thread at start, exit and when idle
    dmq::UnicastDelegate<void()> m_startHandler;
    dmq::UnicastDelegate<void()> m_exitHandler;
    dmq::UnicastDelegate<void()> m_idleHandler;
    dmq::Duration m_idleInterval = dmq::Duration::zero();

    std::atomic<bool> m_exit;

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::atomic<dmq::Duration> m_watchdogTimeout;
    Win32Thread* m_watchdogNext = nullptr;

#if defined(DMQ_DATABUS_TOOLS)
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
/// keeps compiling unchanged against the Win32 port.
using Thread = Win32Thread;

} // namespace dmq::os


#endif
