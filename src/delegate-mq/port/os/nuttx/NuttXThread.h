#ifndef _THREAD_NUTTX_H
#define _THREAD_NUTTX_H

/// @file NuttXThread.h
/// @brief NuttX RTOS implementation of the DelegateMQ IThread interface.
///
/// @note This implementation is a basic port. For reference, the stdlib and win32
/// implementations provide additional features:
/// 1. Synchronized Startup: CreateThread() blocks until the worker thread is ready.
///
/// **Key Features:**
/// * **Task Integration:** Wraps `pthread_create` to establish a dedicated worker loop.
///   NuttX implements the real POSIX threading API, so this port builds on
///   pthread_t/pthread_create/pthread_join directly rather than a bespoke
///   kernel-object wrapper.
/// * **FullPolicy Support:** Configurable back-pressure (DROP or TIMEOUT) when the
///   message queue is full.
/// * **Priority Support:** Normal and High priorities, using POSIX mq_send()'s
///   native msg_prio argument -- see NuttXDelegateQueue.h.
/// * **Queue-Based Dispatch:** Uses `NuttXDelegateQueue` (a thin RAII wrapper
///   around a POSIX mqueue) to receive and process incoming delegate
///   messages in a thread-safe manner.
/// * **Watchdog Integration:** Optional heartbeat mechanism detects stalled or deadlocked
///   threads. Enable by passing a timeout to CreateThread(). Requires
///   Timer::ProcessTimers() to be called from a context that can preempt watched threads
///   -- typically a hardware timer ISR or the highest-priority task in the system.
///
/// @note UNVERIFIED: written against documented NuttX POSIX API behavior.
/// No NuttX toolchain/simulator is available in this development
/// environment to build and run it. Review carefully, and exercise on real
/// NuttX hardware or `nuttx/boards/sim` before relying on it in production.

#include "delegate/IThread.h"
#include "port/os/common/ThreadMsg.h"
#include "NuttXDelegateQueue.h"
#include "extras/util/Timer.h"
#include <pthread.h>
#include <semaphore.h>
#include <memory>
#include <atomic>
#include <string>
#include <optional>

namespace dmq::os {

/// @brief Policy applied when the thread message queue is full. See dmq::FullPolicy
/// in DelegateOpt.h for the canonical definition, shared by every dmq::os::Thread port.
using FullPolicy = dmq::FullPolicy;

class NuttXThread : public dmq::IThread
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
    static const size_t DEFAULT_QUEUE_SIZE = dmq::DEFAULT_QUEUE_SIZE;

    /// Constructor
    /// @param threadName Name for the NuttX pthread
    /// @param maxQueueSize Max number of messages in queue (0 = Default dmq::DEFAULT_QUEUE_SIZE)
    /// @param fullPolicy Action when queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    /// @param cpuName Optional CPU/Core name grouping for monitoring tools.
    NuttXThread(const char* threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const char* cpuName = "");

    NuttXThread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const std::string& cpuName = "")
        : NuttXThread(threadName.c_str(), maxQueueSize, fullPolicy, dispatchTimeout, cpuName.c_str()) {}

    ~NuttXThread();

    /// Called once to create the worker thread. If watchdogTimeout value
    /// provided, the maximum watchdog interval is used. Otherwise no watchdog.
    /// @param[in] watchdogTimeout - optional watchdog timeout.
    /// @return TRUE if thread is created. FALSE otherwise.
    bool CreateThread(std::optional<dmq::Duration> watchdogTimeout = std::nullopt);
    void ExitThread();

    pthread_t GetThreadId();
    static pthread_t GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    /// Set the pthread scheduling priority (SCHED_FIFO). Can be called
    /// before or after CreateThread().
    void SetThreadPriority(int priority);

    dmq::xstring GetThreadName() { return THREAD_NAME; }

    /// Get current queue size
    size_t GetQueueSize();

    /// Sleep for a duration.
    /// @param[in] timeout - the duration to sleep.
    static void Sleep(dmq::Duration timeout);

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
    NuttXThread(const NuttXThread&) = delete;
    NuttXThread& operator=(const NuttXThread&) = delete;

    // NuttXThread entry point
    static void* Process(void* arg);
    void Run();

    /// Check watchdog is expired. Called from Timer::ProcessTimers() context.
    void WatchdogCheck();

    /// Get registry head using the "Immortal" Pattern
    static NuttXThread*& GetWatchdogHead();

    /// Get registry lock using the "Immortal" Pattern
    static dmq::RecursiveMutex& GetWatchdogLock();

    const dmq::xstring THREAD_NAME;
    const dmq::xstring CPU_NAME;
    const size_t m_queueSize;
    const FullPolicy FULL_POLICY;
    const dmq::Duration m_dispatchTimeout;
    int m_priority;

    pthread_t m_thread{};
    NuttXDelegateQueue m_queue;
    sem_t m_exitSem{}; // Semaphore to signal thread completion
    std::atomic<bool> m_exit = false;
    std::atomic<bool> m_created = false;
    bool* m_selfExitPtr = nullptr;

    // Set when the thread terminates itself (ExitThread() called from within
    // its own dispatched callback). A self-exiting thread cannot join or free
    // its own stack, so a later ExitThread() call (typically from
    // ~NuttXThread(), made from a different thread context) checks this to
    // skip the message-send/semaphore handshake and go straight to the
    // pthread_join() cleanup instead.
    std::atomic<bool> m_selfExited = false;

    // Stack size in bytes, applied via pthread_attr_setstacksize() before
    // pthread_create(); NuttX allocates and frees the stack itself, unlike
    // the Zephyr/CMSIS-RTOS2 ports which must k_aligned_alloc()/k_free() it
    // manually.
    static const size_t STACK_SIZE = 8192;

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::atomic<dmq::Duration> m_watchdogTimeout;
    NuttXThread* m_watchdogNext = nullptr;

#if defined(DMQ_DATABUS_TOOLS)
    pthread_mutex_t m_statMutex; // Mutex to protect statistics
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
/// keeps compiling unchanged against the NuttX port.
using Thread = NuttXThread;

} // namespace dmq::os

#endif // _THREAD_NUTTX_H
