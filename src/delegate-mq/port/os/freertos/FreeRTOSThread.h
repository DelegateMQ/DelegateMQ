#ifndef _THREAD_FREERTOS_H
#define _THREAD_FREERTOS_H

/// @file FreeRTOSThread.h
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
/// * **Priority Support:** Normal and High priorities (High jumps the FIFO via
///   `FreeRTOSDelegateQueue::Send`'s highPriority flag).
/// * **Queue-Based Dispatch:** Uses `FreeRTOSDelegateQueue` (a thin RAII wrapper
///   around a FreeRTOS `QueueHandle_t`) to receive and process incoming delegate
///   messages in a thread-safe manner.
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
#include "port/os/common/ThreadMsg.h"
#include "FreeRTOSDelegateQueue.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string>
#include <memory>
#include <atomic>
#include <optional>

namespace dmq::os {

/// @brief Policy applied when the FreeRTOS task queue is full (see dmq::FullPolicy
/// in DelegateOpt.h for the canonical definition, shared by every dmq::os::Thread port).
/// DROP/FAULT map to xQueueSend() with timeout 0; TIMEOUT to a finite timeout.
/// FAULT is the default. For embedded targets where the caller may be an ISR or
/// high-priority task, consider DROP to avoid blocking at an unsafe context.
using FullPolicy = dmq::FullPolicy;

class FreeRTOSThread : public dmq::IThread
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

    /// Default queue size if 0 is passed
    static const size_t DEFAULT_QUEUE_SIZE = dmq::DEFAULT_QUEUE_SIZE;

    /// Constructor
    /// @param threadName Name for the FreeRTOS task
    /// @param maxQueueSize Max number of messages in queue (0 = Default dmq::DEFAULT_QUEUE_SIZE)
    /// @param fullPolicy Action when queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    /// @param cpuName Optional CPU/Core name grouping for monitoring tools.
    FreeRTOSThread(const char* threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const char* cpuName = "");

    FreeRTOSThread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const std::string& cpuName = "")
        : FreeRTOSThread(threadName.c_str(), maxQueueSize, fullPolicy, dispatchTimeout, cpuName.c_str()) {}

    /// Destructor
    ~FreeRTOSThread();

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
    dmq::xstring GetThreadName() { return THREAD_NAME; }

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

    /// @brief Manually update the watchdog alive timestamp.
    /// @details The Run() loop refreshes the timestamp automatically on every iteration.
    /// Call this from inside long-running message handlers to prevent a false watchdog
    /// alarm when a handler legitimately takes longer than watchdogTimeout.
    void ThreadCheck();

    /// @brief Check all registered threads for watchdog expiration.
    /// @details Call this from the highest-priority task in the system.
    static void WatchdogCheckAll();

#if defined(DMQ_DATABUS_TOOLS)
    /// @brief Capture and reset windowed statistics.
    ThreadStats SnapshotStats();
#endif

private:
    FreeRTOSThread(const FreeRTOSThread&) = delete;
    FreeRTOSThread& operator=(const FreeRTOSThread&) = delete;

    /// Entry point for the thread
    static void Process(void* instance);

    // Run loop called by Process
    void Run();

    /// Check watchdog is expired for this instance. 
    void WatchdogCheck();

    /// Get registry head using the "Immortal" Pattern
    static FreeRTOSThread*& GetWatchdogHead()
    {
        static FreeRTOSThread* head = nullptr;
        return head;
    }

    /// Get registry lock using the "Immortal" Pattern
    static dmq::RecursiveMutex& GetWatchdogLock()
    {
        static dmq::RecursiveMutex* lock = new dmq::RecursiveMutex();
        return *lock;
    }

    const dmq::xstring THREAD_NAME;
    const dmq::xstring CPU_NAME;
    const FullPolicy FULL_POLICY;
    const dmq::Duration m_dispatchTimeout;
    size_t m_queueSize;
    int m_priority;

    TaskHandle_t m_thread = nullptr;
    FreeRTOSDelegateQueue m_queue;
    SemaphoreHandle_t m_exitSem = nullptr; // Synchronization for safe destruction
    std::atomic<bool> m_exit = false;
    bool* m_selfExitPtr = nullptr;

    // Static allocation support
    StackType_t* m_stackBuffer = nullptr;
    uint32_t m_stackSize = 4096; // Default size (words)
    StaticTask_t m_tcb;          // TCB storage for static creation

    // Watchdog related members
    std::atomic<uint32_t> m_lastAliveTime{0};
    std::atomic<uint32_t> m_watchdogTimeout{0};
    FreeRTOSThread* m_watchdogNext = nullptr;

#if defined(DMQ_DATABUS_TOOLS)
    // Monitoring statistics members
    std::atomic<size_t> m_queueDepthMaxWindow{0};
    std::atomic<size_t> m_queueDepthMaxAll{0};

    // Use a mutex to protect 64-bit stats on 32-bit platforms without libatomic
    dmq::RecursiveMutex m_statsLock;
    int64_t m_latencyTotalWindow{0};
    uint32_t m_latencyCountWindow{0};
    int64_t m_latencyMaxWindow{0};
    int64_t m_latencyMaxAll{0};

    int64_t m_invokeTotalWindow{0};
    uint32_t m_invokeCountWindow{0};
    int64_t m_invokeMaxWindow{0};
    int64_t m_invokeMaxAll{0};

    uint64_t m_dispatchCountAll{0};
#endif
};

/// @brief Backward-compatible name: existing code referencing dmq::os::Thread
/// keeps compiling unchanged against the FreeRTOS port.
using Thread = FreeRTOSThread;

} // namespace dmq::os


#endif
