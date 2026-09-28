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
#include "delegate/UnicastDelegate.h"
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

/// @brief What ExitThread() does with queued messages. See dmq::ExitPolicy in DelegateOpt.h.
using ExitPolicy = dmq::ExitPolicy;

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

    /// Shut down the worker thread.
    /// @param[in] policy - DRAIN (default) invokes every message queued before this
    ///   call first; DISCARD invokes only the message already running and cancels
    ///   the rest (see dmq::ExitPolicy). Called from the thread's own message
    ///   handler (a self-exit), queued messages are always discarded.
    void ExitThread(ExitPolicy policy = ExitPolicy::DRAIN);

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
    void SetStartHandler(const dmq::UnicastDelegate<void()>& handler) { DMQ_ASSERT_TRUE(!IsThreadCreated()); m_startHandler = handler; }
    void SetStartHandler(dmq::UnicastDelegate<void()>&& handler) { DMQ_ASSERT_TRUE(!IsThreadCreated()); m_startHandler = std::move(handler); }

    /// @brief Register a handler invoked on the worker thread after it processes
    /// its last message, as the thread exits. Use it to undo SetStartHandler() setup.
    /// @details Must be called while the thread is not running; calling it on a
    /// running thread faults. The handler is copied when the thread starts.
    /// ExitThread() from another thread returns after it completes. If the thread
    /// exits itself (ExitThread() from a handler on this thread), it runs after the
    /// owning FreeRTOSThread may already be destroyed, so it must not touch that object.
    void SetExitHandler(const dmq::UnicastDelegate<void()>& handler) { DMQ_ASSERT_TRUE(!IsThreadCreated()); m_exitHandler = handler; }
    void SetExitHandler(dmq::UnicastDelegate<void()>&& handler) { DMQ_ASSERT_TRUE(!IsThreadCreated()); m_exitHandler = std::move(handler); }

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
        DMQ_ASSERT_TRUE(!IsThreadCreated());
        DMQ_ASSERT_TRUE(interval > dmq::Duration::zero());
        m_idleHandler = handler;
        m_idleInterval = interval;
    }
    void SetIdleHandler(dmq::UnicastDelegate<void()>&& handler, dmq::Duration interval = dmq::THREAD_IDLE_INTERVAL)
    {
        DMQ_ASSERT_TRUE(!IsThreadCreated());
        DMQ_ASSERT_TRUE(interval > dmq::Duration::zero());
        m_idleHandler = std::move(handler);
        m_idleInterval = interval;
    }

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

    // Optional handler invoked when a message is dropped (FullPolicy::DROP or TIMEOUT)
    dmq::UnicastDelegate<void(size_t)> m_droppedHandler;

    // Optional handlers invoked on the worker thread at start, exit and when idle
    dmq::UnicastDelegate<void()> m_startHandler;
    dmq::UnicastDelegate<void()> m_exitHandler;
    dmq::UnicastDelegate<void()> m_idleHandler;
    dmq::Duration m_idleInterval = dmq::Duration::zero();
    SemaphoreHandle_t m_exitSem = nullptr; // Synchronization for safe destruction
    SemaphoreHandle_t m_startSem = nullptr; // Given by Run() once the start handler has run
    bool m_startSync = false; // CreateThread() waits for the start handler (scheduler running)
    std::atomic<bool> m_exit = false;
    std::atomic<bool> m_discard = false; // ExitPolicy::DISCARD: cancel queued messages instead of invoking
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
