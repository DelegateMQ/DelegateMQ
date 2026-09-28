#ifndef _THREAD_STD_H
#define _THREAD_STD_H

/// @file StdlibThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Standard C++ implementation of the DelegateMQ IThread interface.
///
/// @details
/// This class provides a cross-platform implementation of the `IThread` interface using 
/// standard C++11 primitives (`std::thread`, `std::mutex`, `std::condition_variable`). 
/// It creates a dedicated worker thread with an event loop capable of processing 
/// asynchronous delegates and system messages.
///
/// **Key Features:**
/// * **Priority Queue:** Uses `std::priority_queue` to ensure high-priority delegate 
///   messages (e.g., system signals) are processed before lower-priority ones.
/// * **Queue Full Policy:** Configurable `FullPolicy` (DROP or TIMEOUT), always enforced --
///   `maxQueueSize == 0` falls back to `dmq::THREAD_DESKTOP_QUEUE_SIZE` rather than disabling
///   the cap, since this port backs its queue with a plain `std::deque` and would otherwise
///   grow without bound if the destination thread is dead/stuck. TIMEOUT waits up to
///   `dispatchTimeout` for the consumer before logging and dropping; DROP silently discards
///   immediately. FAULT (the default) triggers a system fault.
/// * **Watchdog Integration:** Includes a built-in heartbeat mechanism. If the thread loop 
///   stalls (deadlock or infinite loop), the watchdog timer detects the failure.
/// * **Synchronized Start:** Uses `std::promise` and `std::future` to ensure the thread 
///   is fully initialized and running before `CreateThread()` returns.
/// * **Debug Support:** Sets the native thread name (on supported OSs like Windows) to 
///   aid debugging in IDEs.

#include "delegate/IThread.h"
#include "delegate/UnicastDelegate.h"
#include "./extras/util/Timer.h"
#include "port/os/common/ThreadMsg.h"
#include <thread>
#include <deque>
#include <atomic>
#include <condition_variable>
#include <future>
#include <optional>

namespace dmq::os {

/// @brief Policy applied when the thread message queue is full. See dmq::FullPolicy
/// in DelegateOpt.h for the canonical definition, shared by every dmq::os::Thread port.
using FullPolicy = dmq::FullPolicy;

/// @brief What ExitThread() does with queued messages. See dmq::ExitPolicy in DelegateOpt.h.
using ExitPolicy = dmq::ExitPolicy;

/// @brief Cross-platform thread for any system supporting C++11 std::thread (e.g. Windows, Linux).
/// @details The StdlibThread class creates a worker thread capable of dispatching and
/// invoking asynchronous delegates.
class StdlibThread : public dmq::IThread
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
    StdlibThread(const char* threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const char* cpuName = "");
    StdlibThread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const std::string& cpuName = "")
        : StdlibThread(threadName.c_str(), maxQueueSize, fullPolicy, dispatchTimeout, cpuName.c_str()) {}

    /// Destructor
    ~StdlibThread();

    /// Called once to create the worker thread. If watchdogTimeout value 
    /// provided, the maximum watchdog interval is used. Otherwise no watchdog.
    /// @param[in] watchdogTimeout - optional watchdog timeout.
    /// @return TRUE if thread is created. FALSE otherwise. 
    bool CreateThread(std::optional<dmq::Duration> watchdogTimeout = std::nullopt);

    /// Called once at program exit to shut down the worker thread.
    /// @param[in] policy - DRAIN (default) invokes every message queued before this
    ///   call first; DISCARD invokes only the message already running and cancels
    ///   the rest (see dmq::ExitPolicy). Called from the thread's own message
    ///   handler (a self-exit), queued messages are always discarded.
    void ExitThread(ExitPolicy policy = ExitPolicy::DRAIN);

    /// Get the ID of this thread instance
    std::thread::id GetThreadId();

    /// Get the ID of the currently executing thread
    static std::thread::id GetCurrentThreadId();

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
    void SetStartHandler(const dmq::UnicastDelegate<void()>& handler) { DMQ_ASSERT_TRUE(!m_thread); m_startHandler = handler; }
    void SetStartHandler(dmq::UnicastDelegate<void()>&& handler) { DMQ_ASSERT_TRUE(!m_thread); m_startHandler = std::move(handler); }

    /// @brief Register a handler invoked on the worker thread after it processes
    /// its last message, as the thread exits. Use it to undo SetStartHandler() setup.
    /// @details Must be called while the thread is not running; calling it on a
    /// running thread faults. The handler is copied when the thread starts.
    /// ExitThread() from another thread returns after it completes. If the thread
    /// exits itself (ExitThread() from a handler on this thread), it runs after the
    /// owning StdlibThread may already be destroyed, so it must not touch that object.
    void SetExitHandler(const dmq::UnicastDelegate<void()>& handler) { DMQ_ASSERT_TRUE(!m_thread); m_exitHandler = handler; }
    void SetExitHandler(dmq::UnicastDelegate<void()>&& handler) { DMQ_ASSERT_TRUE(!m_thread); m_exitHandler = std::move(handler); }

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
        DMQ_ASSERT_TRUE(!m_thread);
        DMQ_ASSERT_TRUE(interval > dmq::Duration::zero());
        m_idleHandler = handler;
        m_idleInterval = interval;
    }
    void SetIdleHandler(dmq::UnicastDelegate<void()>&& handler, dmq::Duration interval = dmq::THREAD_IDLE_INTERVAL)
    {
        DMQ_ASSERT_TRUE(!m_thread);
        DMQ_ASSERT_TRUE(interval > dmq::Duration::zero());
        m_idleHandler = std::move(handler);
        m_idleInterval = interval;
    }

    /// @brief Manually update the watchdog alive timestamp.
    /// @details The Process() loop refreshes the timestamp automatically on every iteration.
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
    StdlibThread(const StdlibThread&) = delete;
    StdlibThread& operator=(const StdlibThread&) = delete;

    /// Entry point for the thread
    void Process();

    void SetThreadName(std::thread::native_handle_type handle, const dmq::xstring& name);

    /// Check watchdog is expired. This function is called by the thread 
    /// the calls Timer::ProcessTimers(). This function is thread-safe.
    /// In a real-time OS, Timer::ProcessTimers() typically is called by the highest
    /// priority task in the system.
    void WatchdogCheck();

    /// @brief Returns the head of the watchdog linked list.
    static StdlibThread*& GetWatchdogHead();

    /// @brief Returns the recursive mutex for protecting the watchdog list.
    static dmq::RecursiveMutex& GetWatchdogLock();

    std::optional<std::thread> m_thread;
    std::atomic<bool> m_exit;

#ifdef DMQ_ALLOCATOR
    std::deque<std::shared_ptr<ThreadMsg>, stl_allocator<std::shared_ptr<ThreadMsg>>> m_highQueue;
    std::deque<std::shared_ptr<ThreadMsg>, stl_allocator<std::shared_ptr<ThreadMsg>>> m_normalQueue;
#else
    std::deque<std::shared_ptr<ThreadMsg>> m_highQueue;
    std::deque<std::shared_ptr<ThreadMsg>> m_normalQueue;
#endif
    std::mutex m_mutex;
    std::condition_variable m_cv;

    // Condition variable to wake up blocked producers when space is available
    std::condition_variable m_cvNotFull;

    const dmq::xstring THREAD_NAME;
    const dmq::xstring CPU_NAME;

    // Max queue size (0 = unlimited)
    const size_t MAX_QUEUE_SIZE;

    // Policy when queue is full
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

    // Promise and future to synchronize thread start (constructed lazily in CreateThread)
    std::optional<std::promise<void>> m_threadStartPromise;
    std::optional<std::future<void>> m_threadStartFuture;

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::atomic<dmq::Duration> m_watchdogTimeout;
    StdlibThread* m_watchdogNext = nullptr;

#if defined(DMQ_DATABUS_TOOLS)
    // Separate mutex for statistics to reduce contention on m_mutex
    std::mutex m_statsMutex;

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
/// keeps compiling unchanged against the stdlib port.
using Thread = StdlibThread;

} // namespace dmq::os


#endif
