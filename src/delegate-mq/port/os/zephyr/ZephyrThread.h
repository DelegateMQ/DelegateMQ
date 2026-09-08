#ifndef _THREAD_ZEPHYR_H
#define _THREAD_ZEPHYR_H

/// @file ZephyrThread.h
/// @brief Zephyr RTOS implementation of the DelegateMQ IThread interface.
///
/// @note This implementation is a basic port. For reference, the stdlib and win32
/// implementations provide additional features:
/// 1. Synchronized Startup: CreateThread() blocks until the worker thread is ready.
///
/// **Key Features:**
/// * **Task Integration:** Wraps `k_thread_create` to establish a dedicated worker loop.
/// * **FullPolicy Support:** Configurable back-pressure (DROP or TIMEOUT) when the
///   message queue is full.
/// * **Priority Support:** Normal and High priorities. Since Zephyr's `k_msgq` has
///   no native "send to front", `ZephyrDelegateQueue` keeps two `k_msgq` instances
///   (mirroring the desktop two-deque model) and always drains the high-priority
///   one first -- see ZephyrDelegateQueue.h for the tradeoffs this implies.
/// * **Queue-Based Dispatch:** Uses `ZephyrDelegateQueue` (a thin RAII wrapper
///   around a pair of `k_msgq` instances) to receive and process incoming
///   delegate messages in a thread-safe manner.
/// * **Watchdog Integration:** Optional heartbeat mechanism detects stalled or deadlocked
///   threads. Enable by passing a timeout to CreateThread(). Requires
///   Timer::ProcessTimers() to be called from a context that can preempt watched threads
///   -- typically a hardware timer ISR or the highest-priority task in the system.

#include "delegate/IThread.h"
#include "port/os/common/ThreadMsg.h"
#include "ZephyrDelegateQueue.h"
#include "extras/util/Timer.h"
#include <zephyr/kernel.h>
#include <memory>
#include <atomic>
#include <string>

namespace dmq::os {

/// @brief Policy applied when the thread message queue is full. See dmq::FullPolicy
/// in DelegateOpt.h for the canonical definition, shared by every dmq::os::Thread port.
using FullPolicy = dmq::FullPolicy;

class ZephyrThread : public dmq::IThread
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
    /// @param threadName Name for the Zephyr thread
    /// @param maxQueueSize Max number of messages in queue (0 = Default dmq::DEFAULT_QUEUE_SIZE)
    /// @param fullPolicy Action when queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    /// @param cpuName Optional CPU/Core name grouping for monitoring tools.
    ZephyrThread(const char* threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const char* cpuName = "");

    ZephyrThread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const std::string& cpuName = "")
        : ZephyrThread(threadName.c_str(), maxQueueSize, fullPolicy, dispatchTimeout, cpuName.c_str()) {}

    ~ZephyrThread();

    /// Called once to create the worker thread. If watchdogTimeout value
    /// provided, the maximum watchdog interval is used. Otherwise no watchdog.
    /// @param[in] watchdogTimeout - optional watchdog timeout.
    /// @return TRUE if thread is created. FALSE otherwise.
    bool CreateThread(std::optional<dmq::Duration> watchdogTimeout = std::nullopt);
    void ExitThread();

    // Note: k_tid_t is a struct k_thread* in Zephyr
    k_tid_t GetThreadId();
    static k_tid_t GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    /// Set the Zephyr Priority.
    /// Can be called before or after CreateThread().
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
    ZephyrThread(const ZephyrThread&) = delete;
    ZephyrThread& operator=(const ZephyrThread&) = delete;

    // ZephyrThread entry point
    static void Process(void* p1, void* p2, void* p3);
    void Run();

    /// Check watchdog is expired. Called from Timer::ProcessTimers() context.
    void WatchdogCheck();

    /// Get registry head using the "Immortal" Pattern
    static ZephyrThread*& GetWatchdogHead();

    /// Get registry lock using the "Immortal" Pattern
    static dmq::RecursiveMutex& GetWatchdogLock();

    const dmq::xstring THREAD_NAME;
    const dmq::xstring CPU_NAME;
    const size_t m_queueSize;
    const FullPolicy FULL_POLICY;
    const dmq::Duration m_dispatchTimeout;
    int m_priority;

    // Zephyr Kernel Objects
    struct k_thread m_thread;
    ZephyrDelegateQueue m_queue;
    struct k_sem m_exitSem; // Semaphore to signal thread completion
    std::atomic<bool> m_exit = false;
    bool* m_selfExitPtr = nullptr;

    // Custom deleter for Zephyr kernel memory (wraps k_free)
    using ZephyrDeleter = void(*)(void*);

    // Dynamically allocated stack, managed by unique_ptr but allocated via
    // k_aligned_alloc and freed via k_free. Also doubles as the
    // "is thread created" sentinel checked throughout this class.
    std::unique_ptr<char, ZephyrDeleter> m_stackMemory{nullptr, k_free};

    // Stack size in bytes
    static const size_t STACK_SIZE = 2048;

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::atomic<dmq::Duration> m_watchdogTimeout;
    ZephyrThread* m_watchdogNext = nullptr;

#if defined(DMQ_DATABUS_TOOLS)
    struct k_mutex m_statMutex; // Mutex to protect statistics
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
/// keeps compiling unchanged against the Zephyr port.
using Thread = ZephyrThread;

} // namespace dmq::os

#endif // _THREAD_ZEPHYR_H
