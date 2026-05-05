#ifndef _THREAD_WIN32_H
#define _THREAD_WIN32_H

/// @file Thread.h
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
/// * **Queue Full Policy:** Configurable `FullPolicy` (DROP, FAULT, or TIMEOUT) when `maxQueueSize > 0`.
///   TIMEOUT waits up to `dispatchTimeout` for the consumer before logging and dropping;
///   DROP silently discards immediately. FAULT (the default) triggers a system fault.
/// * **Watchdog Integration:** Includes a built-in heartbeat mechanism. If the thread loop
///   stalls (deadlock or infinite loop), the watchdog timer detects the failure.
/// * **Synchronized Start:** Uses a Win32 manual-reset event to ensure the thread
///   is fully initialized and running before `CreateThread()` returns.
/// * **Debug Support:** Sets the native thread name via `SetThreadDescription()` to
///   aid debugging in Visual Studio.

#include "delegate/IThread.h"
#include "./extras/util/Timer.h"
#include "ThreadMsg.h"
#include <queue>
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

/// @brief Windows-native thread for systems using the Win32 API.
/// @details The Thread class creates a worker thread capable of dispatching and
/// invoking asynchronous delegates.
class Thread : public dmq::IThread
{
public:
#if defined(DMQ_DATABUS_TOOLS)
    /// @brief Statistics captured for thread monitoring.
    struct ThreadStats {
        std::string cpu_name;
        std::string thread_name;
        size_t queue_depth;           // Current depth
        size_t queue_depth_max_window;// Max depth since last snapshot
        size_t queue_depth_max_all;   // All-time max depth
        size_t queue_size_limit;      // Max allowed
        float latency_avg_ms;        // Avg wait in window
        float latency_max_window_ms; // Max wait since last snapshot
        float latency_max_all_ms;    // All-time max wait
        uint64_t dispatch_count;      // Total dispatches (all-time)
    };
#endif

    /// Constructor
    /// @param threadName The name of the thread for debugging.
    /// @param maxQueueSize The maximum number of messages allowed in the queue.
    ///                     0 means unlimited (no back pressure).
    /// @param fullPolicy When the queue is full: FAULT (default), DROP, or TIMEOUT.
    ///                   Only meaningful when maxQueueSize > 0.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    /// @param cpuName Optional CPU/Core name grouping for monitoring tools.
    Thread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const std::string& cpuName = "");

    /// Destructor
    virtual ~Thread();

    /// Called once to create the worker thread. If watchdogTimeout value
    /// provided, the maximum watchdog interval is used. Otherwise no watchdog.
    /// @param[in] watchdogTimeout - optional watchdog timeout.
    /// @return TRUE if thread is created. FALSE otherwise.
    bool CreateThread(std::optional<dmq::Duration> watchdogTimeout = std::nullopt);

    /// Called once at program exit to shut down the worker thread
    void ExitThread();

    /// Get the ID of this thread instance
    DWORD GetThreadId();

    /// Get the ID of the currently executing thread
    static DWORD GetCurrentThreadId();

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
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

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
    static Thread*& GetWatchdogHead();

    /// Get registry lock using the "Immortal" Pattern
    static dmq::RecursiveMutex& GetWatchdogLock();

    /// @brief Manually update the watchdog alive timestamp.
    /// @details The Run() loop refreshes the timestamp automatically on every iteration.
    /// Call this from inside long-running message handlers to prevent a false watchdog
    /// alarm when a handler legitimately takes longer than watchdogTimeout.
    void ThreadCheck();

    HANDLE m_hThread = NULL;
    DWORD m_threadId = 0;

    // Manual-reset event to synchronize thread startup
    HANDLE m_hStartEvent = NULL;

    CRITICAL_SECTION m_cs;

    // Condition variable to wake up consumers when a message is enqueued
    CONDITION_VARIABLE m_cvNotEmpty;

    // Condition variable to wake up blocked producers when space is available
    CONDITION_VARIABLE m_cvNotFull;

    std::priority_queue<std::shared_ptr<ThreadMsg>,
        std::vector<std::shared_ptr<ThreadMsg>>,
        ThreadMsgComparator> m_queue;

    const std::string THREAD_NAME;
    const std::string CPU_NAME;

    // Max queue size for back pressure (0 = unlimited)
    const size_t MAX_QUEUE_SIZE;

    // Policy applied when the thread message queue is full.
    const FullPolicy FULL_POLICY;

    // Timeout duration for TIMEOUT policy
    const dmq::Duration m_dispatchTimeout;

    std::atomic<bool> m_exit;

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::unique_ptr<dmq::util::Timer> m_watchdogTimer;
    dmq::ScopedConnection m_watchdogTimerConn;
    std::atomic<dmq::Duration> m_watchdogTimeout;

#if defined(DMQ_DATABUS_TOOLS)
    // Monitoring statistics members
    size_t m_queueDepthMaxWindow = 0;
    size_t m_queueDepthMaxAll = 0;

    dmq::Duration m_latencyTotalWindow = dmq::Duration(0);
    uint32_t m_latencyCountWindow = 0;
    dmq::Duration m_latencyMaxWindow = dmq::Duration(0);
    dmq::Duration m_latencyMaxAll = dmq::Duration(0);
    uint64_t m_dispatchCountAll = 0;
#endif
};

} // namespace dmq::os


#endif
