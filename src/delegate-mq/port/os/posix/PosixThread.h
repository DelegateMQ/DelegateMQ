#ifndef _THREAD_POSIX_H
#define _THREAD_POSIX_H

/// @file PosixThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Raw POSIX implementation of the DelegateMQ IThread interface.
///
/// @details
/// This class provides a native POSIX implementation of the `IThread` interface using
/// `pthread_create`/`pthread_mutex_t`/`pthread_cond_t` directly, rather than going through
/// `std::thread`/`std::mutex`/`std::condition_variable` the way `StdlibThread` does. It
/// creates a dedicated worker thread with an event loop capable of processing asynchronous
/// delegates and system messages -- mirroring `Win32Thread`'s relationship to `StdlibThread`
/// (native OS API instead of the C++ standard library), but for POSIX.
///
/// `dmq::Mutex`, `dmq::ConditionVariable`, `dmq::Clock`, and `dmq::ThisThread` are NOT
/// reimplemented here -- they still resolve to `std::mutex`/`std::condition_variable`/
/// `std::chrono::steady_clock`/`std::this_thread` (see `DelegateOpt.h`'s
/// `DMQ_THREAD_STDLIB || DMQ_THREAD_WIN32 || DMQ_THREAD_QT || DMQ_THREAD_POSIX` branches),
/// since those are already thin, efficient wrappers over these same POSIX primitives under
/// glibc/libstdc++. This file is the only thing `DMQ_THREAD_POSIX` adds.
///
/// **Key Features:**
/// * **Priority Queue:** Separate high/normal `std::deque`s to ensure high-priority delegate
///   messages (e.g. system signals) are processed before lower-priority ones.
/// * **Queue Full Policy:** Configurable `FullPolicy` (DROP, FAULT, or TIMEOUT), always enforced --
///   `maxQueueSize == 0` falls back to `dmq::THREAD_DESKTOP_QUEUE_SIZE` rather than disabling
///   the cap, since this port backs its queue with a plain `std::deque` and would otherwise
///   grow without bound if the destination thread is dead/stuck. TIMEOUT waits up to
///   `dispatchTimeout` for the consumer before logging and dropping; DROP silently discards
///   immediately. FAULT (the default) triggers a system fault.
/// * **Monotonic Timed Waits:** The condition variables are created with `pthread_condattr_setclock(
///   CLOCK_MONOTONIC)` so `pthread_cond_timedwait()` deadlines are immune to `CLOCK_REALTIME`
///   jumps (NTP steps, manual clock changes) -- unlike a naive POSIX cv using the default clock.
///   `pthread_condattr_setclock()` itself is a common POSIX extension (present on Linux's glibc
///   and musl) rather than a portable POSIX-standard guarantee -- e.g. not implemented on Darwin/
///   macOS -- which is the one place this port leans on more than the base POSIX spec.
/// * **Watchdog Integration:** Includes a built-in heartbeat mechanism. If the thread loop
///   stalls (deadlock or infinite loop), the watchdog timer detects the failure.
/// * **Synchronized Start:** Uses a dedicated mutex/condition-variable pair to ensure the
///   thread is fully initialized and running before `CreateThread()` returns.

#include "delegate/IThread.h"
#include "delegate/UnicastDelegate.h"
#include "./extras/util/Timer.h"
#include "port/os/common/ThreadMsg.h"
#include <pthread.h>
#include <deque>
#include <atomic>
#include <optional>
#include <string>

namespace dmq::os {

/// @brief Policy applied when the thread message queue is full. See dmq::FullPolicy
/// in DelegateOpt.h for the canonical definition, shared by every dmq::os::Thread port.
using FullPolicy = dmq::FullPolicy;

/// @brief Raw POSIX (pthreads) thread for Linux and other POSIX systems.
/// @details The PosixThread class creates a worker thread capable of dispatching and
/// invoking asynchronous delegates.
class PosixThread : public dmq::IThread
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
    PosixThread(const char* threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const char* cpuName = "");
    PosixThread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const std::string& cpuName = "")
        : PosixThread(threadName.c_str(), maxQueueSize, fullPolicy, dispatchTimeout, cpuName.c_str()) {}

    /// Destructor
    virtual ~PosixThread();

    /// Called once to create the worker thread. If watchdogTimeout value
    /// provided, the maximum watchdog interval is used. Otherwise no watchdog.
    /// @param[in] watchdogTimeout - optional watchdog timeout.
    /// @return TRUE if thread is created. FALSE otherwise.
    bool CreateThread(std::optional<dmq::Duration> watchdogTimeout = std::nullopt);

    /// Called once at program exit to shut down the worker thread
    void ExitThread();

    /// Get the ID of this thread instance
    pthread_t GetThreadId();

    /// Get the ID of the currently executing thread
    static pthread_t GetCurrentThreadId();

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
    PosixThread(const PosixThread&) = delete;
    PosixThread& operator=(const PosixThread&) = delete;

    /// pthread entry point trampoline
    static void* ThreadEntry(void* arg);

    /// Entry point for the thread
    void Process();

    /// Check watchdog is expired. This function is called by the thread
    /// that calls dmq::util::Timer::ProcessTimers(). This function is thread-safe.
    /// In a real-time OS, dmq::util::Timer::ProcessTimers() typically is called by the highest
    /// priority task in the system.
    void WatchdogCheck();

    /// Get registry head using the "Immortal" Pattern
    static PosixThread*& GetWatchdogHead();

    /// Get registry lock using the "Immortal" Pattern
    static dmq::RecursiveMutex& GetWatchdogLock();

    /// Compute an absolute CLOCK_MONOTONIC deadline `timeout` from now, for pthread_cond_timedwait().
    static void MakeAbsTimeout(dmq::Duration timeout, struct timespec& ts);

    pthread_t m_thread{};
    bool m_threadCreated = false;

    // Startup synchronization: CreateThread() blocks here until Process() signals it has started.
    pthread_mutex_t m_startMutex;
    pthread_cond_t m_startCv;
    bool m_started = false;

    pthread_mutex_t m_mutex;

    // Condition variable to wake up consumers when a message is enqueued
    pthread_cond_t m_cvNotEmpty;

    // Condition variable to wake up blocked producers when space is available
    pthread_cond_t m_cvNotFull;

#ifdef DMQ_ALLOCATOR
    std::deque<std::shared_ptr<ThreadMsg>, stl_allocator<std::shared_ptr<ThreadMsg>>> m_highQueue;
    std::deque<std::shared_ptr<ThreadMsg>, stl_allocator<std::shared_ptr<ThreadMsg>>> m_normalQueue;
#else
    std::deque<std::shared_ptr<ThreadMsg>> m_highQueue;
    std::deque<std::shared_ptr<ThreadMsg>> m_normalQueue;
#endif

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

    std::atomic<bool> m_exit;

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::atomic<dmq::Duration> m_watchdogTimeout;
    PosixThread* m_watchdogNext = nullptr;

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
/// keeps compiling unchanged against the posix port.
using Thread = PosixThread;

} // namespace dmq::os

#endif
