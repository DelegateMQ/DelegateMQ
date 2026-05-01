#ifndef _THREAD_STD_H
#define _THREAD_STD_H

/// @file Thread.h
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
/// * **Queue Full Policy:** Configurable `FullPolicy` (DROP or TIMEOUT) when `maxQueueSize > 0`.
///   TIMEOUT waits up to `dispatchTimeout` for the consumer before logging and dropping;
///   DROP silently discards immediately. FAULT (the default) triggers a system fault.
/// * **Watchdog Integration:** Includes a built-in heartbeat mechanism. If the thread loop 
///   stalls (deadlock or infinite loop), the watchdog timer detects the failure.
/// * **Synchronized Start:** Uses `std::promise` and `std::future` to ensure the thread 
///   is fully initialized and running before `CreateThread()` returns.
/// * **Debug Support:** Sets the native thread name (on supported OSs like Windows) to 
///   aid debugging in IDEs.

#include "delegate/IThread.h"
#include "./extras/util/Timer.h"
#include "ThreadMsg.h"
#include <thread>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <future>
#include <optional>

namespace dmq::os {

// Comparator for priority queue
struct ThreadMsgComparator {
    bool operator()(const std::shared_ptr<ThreadMsg>& a, const std::shared_ptr<ThreadMsg>& b) const {
        return static_cast<int>(a->GetPriority()) < static_cast<int>(b->GetPriority());
    }
};

/// @brief Policy applied when the thread message queue is full.
/// @details Only meaningful when maxQueueSize > 0.
///   - DROP:    DispatchDelegate() silently discards the message and returns immediately.
///   - FAULT:   DispatchDelegate() triggers a system fault if the queue is full.
///   - TIMEOUT: DispatchDelegate() waits up to dispatchTimeout, then logs and drops.
///
/// Use DROP for high-rate best-effort topics (sensor telemetry, display updates) where
/// a stale sample is preferable to stalling the publisher. Use TIMEOUT for critical topics
/// (commands, state transitions) where every message should be delivered if possible.
/// FAULT is the default.
enum class FullPolicy { DROP, FAULT, TIMEOUT };

/// @brief Cross-platform thread for any system supporting C++11 std::thread (e.g. Windows, Linux).
/// @details The Thread class creates a worker thread capable of dispatching and
/// invoking asynchronous delegates.
class Thread : public dmq::IThread
{
public:
    /// Constructor
    /// @param threadName The name of the thread for debugging.
    /// @param maxQueueSize The maximum number of messages allowed in the queue.
    ///                     0 means unlimited (no back pressure).
    /// @param fullPolicy When the queue is full: FAULT (default), DROP, or TIMEOUT.
    ///                   Only meaningful when maxQueueSize > 0.
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

    /// Called once at program exit to shut down the worker thread
    void ExitThread();

    /// Get the ID of this thread instance
    std::thread::id GetThreadId();

    /// Get the ID of the currently executing thread
    static std::thread::id GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    /// Get thread name
    std::string GetThreadName() { return THREAD_NAME; }

    /// Get size of thread message queue.
    size_t GetQueueSize();

    /// Sleep for a duration.
    /// @param[in] timeout - the duration to sleep.
    static void Sleep(dmq::Duration timeout);

    /// Dispatch and invoke a delegate target on the destination thread.
    /// @param[in] msg - Delegate message containing target function 
    /// arguments.
    virtual bool DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg) override;

    /// Update the last alive time for the watchdog. 
    /// @details Normally called automatically by internal timers. For threads with 
    /// blocking loops (e.g. Network receiver), call this manually to prevent timeouts.
    void ThreadCheck();

private:
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    /// Entry point for the thread
    void Process();

    void SetThreadName(std::thread::native_handle_type handle, const std::string& name);

    /// Check watchdog is expired. This function is called by the thread 
    /// the calls Timer::ProcessTimers(). This function is thread-safe.
    /// In a real-time OS, Timer::ProcessTimers() typically is called by the highest
    /// priority task in the system.
    void WatchdogCheck();

    std::optional<std::thread> m_thread;
    std::atomic<bool> m_exit;

#ifdef DMQ_ALLOCATOR
    std::priority_queue<std::shared_ptr<ThreadMsg>,
        std::vector<std::shared_ptr<ThreadMsg>, stl_allocator<std::shared_ptr<ThreadMsg>>>,
        ThreadMsgComparator> m_queue;
#else
    std::priority_queue<std::shared_ptr<ThreadMsg>,
        std::vector<std::shared_ptr<ThreadMsg>>,
        ThreadMsgComparator> m_queue;
#endif
    std::mutex m_mutex;
    std::condition_variable m_cv;

    // Condition variable to wake up blocked producers when space is available
    std::condition_variable m_cvNotFull;

    const std::string THREAD_NAME;

    // Max queue size (0 = unlimited)
    const size_t MAX_QUEUE_SIZE;

    // Policy when queue is full
    const FullPolicy FULL_POLICY;

    // Timeout duration for TIMEOUT policy
    const dmq::Duration m_dispatchTimeout;

    // Promise and future to synchronize thread start (constructed lazily in CreateThread)
    std::optional<std::promise<void>> m_threadStartPromise;
    std::optional<std::future<void>> m_threadStartFuture;

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::unique_ptr<dmq::util::Timer> m_watchdogTimer;
    dmq::ScopedConnection m_watchdogTimerConn;
    std::unique_ptr<dmq::util::Timer> m_threadTimer;
    dmq::ScopedConnection m_threadTimerConn;
    std::atomic<dmq::Duration> m_watchdogTimeout;
};

} // namespace dmq::os


#endif
