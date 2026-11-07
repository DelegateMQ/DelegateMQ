#ifndef _THREAD_THREADX_H
#define _THREAD_THREADX_H

/// @file ThreadXThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief ThreadX implementation of the DelegateMQ IThread interface.
///
/// @details
/// This class provides a concrete implementation of the `IThread` interface using
/// Azure RTOS ThreadX primitives. It enables DelegateMQ to dispatch asynchronous
/// delegates to a dedicated ThreadX thread.
///
/// @note This implementation is a basic port. For reference, the stdlib and win32
/// implementations provide additional features:
/// 1. Synchronized Startup: CreateThread() blocks until the worker thread is ready.
///
/// **Key Features:**
/// * **Task Integration:** Wraps `tx_thread_create` to establish a dedicated worker loop.
/// * **FullPolicy Support:** Configurable back-pressure (DROP or TIMEOUT) when the
///   message queue is full.
/// * **Priority Support:** Normal and High priorities, as two FIFO queue lanes drained
///   HIGH first, so same-priority messages keep dispatch order (see
///   `ThreadXDelegateQueue` for why one queue + `tx_queue_front_send()` is wrong).
/// * **Queue-Based Dispatch:** Uses `ThreadXDelegateQueue` (a thin RAII wrapper
///   around a ThreadX `TX_QUEUE`) to receive and process incoming delegate
///   messages in a thread-safe manner.
/// * **Priority Control:** Supports runtime priority configuration via `SetThreadPriority`.
/// * **Dynamic Configuration:** Allows configuring stack size and queue depth at construction.
/// * **Graceful Shutdown:** Implements robust termination logic using semaphores to ensure
///   the thread exits cleanly before destruction.
/// * **Watchdog Integration:** Optional heartbeat mechanism detects stalled or deadlocked
///   threads. Enable by passing a timeout to CreateThread(). Requires
///   Thread::WatchdogCheckAll() to be called periodically from a context that can preempt
///   watched threads -- the highest-priority task in the system, not an ISR.
/// * **Start Gate:** Optional `SetStartGate(StartGate*)`, called before `CreateThread()`,
///   holds the thread ready but processing no messages, at any priority, until
///   `StartGate::Open()` releases every thread sharing the gate at once.

#include "delegate/IThread.h"
#include "delegate/UnicastDelegate.h"
#include "port/os/common/ThreadMsg.h"
#include "port/os/common/StartGate.h"
#include "ThreadXDelegateQueue.h"
#include "extras/util/Timer.h"
#include <tx_api.h>
#include <memory>
#include <atomic>
#include <optional>
#include <string>

namespace dmq::os {

using namespace dmq::util;

/// @brief Policy applied when the thread message queue is full. See dmq::FullPolicy
/// in DelegateOpt.h for the canonical definition, shared by every dmq::os::Thread port.
using FullPolicy = dmq::FullPolicy;

/// @brief What ExitThread() does with queued messages. See dmq::ExitPolicy in DelegateOpt.h.
using ExitPolicy = dmq::ExitPolicy;

// Comparator for priority (ThreadX priority is 0 to N-1, where 0 is highest)
// This is used for the priority queue if we had one, but ThreadX uses tx_queue_send/tx_queue_front_send.

class ThreadXThread : public dmq::IThread
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
    static const ULONG DEFAULT_QUEUE_SIZE = dmq::DEFAULT_QUEUE_SIZE;

    /// Constructor
    /// @param threadName Name for the ThreadX thread
    /// @param maxQueueSize Max number of messages in queue (0 = Default dmq::DEFAULT_QUEUE_SIZE)
    /// @param fullPolicy Action when queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    /// @param cpuName Optional CPU/Core name grouping for monitoring tools.
    ThreadXThread(const char* threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const char* cpuName = "");

    ThreadXThread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const std::string& cpuName = "")
        : ThreadXThread(threadName.c_str(), maxQueueSize, fullPolicy, dispatchTimeout, cpuName.c_str()) {}

    /// Destructor
    ~ThreadXThread();

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
    TX_THREAD* GetThreadId();

    /// Get the ID of the currently executing thread
    static TX_THREAD* GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    /// Set the ThreadX Priority (0 = Highest). 
    /// Can be called before or after CreateThread().
    void SetThreadPriority(UINT priority);

    /// Get current priority
    UINT GetThreadPriority();

    /// Get thread name
    dmq::xstring GetThreadName() { return THREAD_NAME; }

    /// Get current queue size
    size_t GetQueueSize();

    /// Sleep for a duration.
    /// @param[in] timeout - the duration to sleep.
    static void Sleep(dmq::Duration timeout);

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
    void SetStartHandler(const dmq::UnicastDelegate<void()>& handler) { DMQ_ASSERT_TRUE(m_thread.tx_thread_id == 0); m_startHandler = handler; }
    void SetStartHandler(dmq::UnicastDelegate<void()>&& handler) { DMQ_ASSERT_TRUE(m_thread.tx_thread_id == 0); m_startHandler = std::move(handler); }

    /// @brief Optional: hold this thread at `gate` once it is created, until
    /// `gate->Open()`. Pass nullptr (the default) to start at once.
    /// @details CreateThread() still runs the start handler and returns when the
    /// thread is ready; the thread then processes nothing, at any priority, until
    /// the gate opens. Use one gate for a group of threads so they all start
    /// together after setup. See port/os/common/StartGate.h. Must be called while
    /// the thread is not running; calling it on a running thread faults.
    void SetStartGate(StartGate* gate) { DMQ_ASSERT_TRUE(m_thread.tx_thread_id == 0); m_startGate.Set(gate); }

    /// @brief Register a handler invoked on the worker thread after it processes
    /// its last message, as the thread exits. Use it to undo SetStartHandler() setup.
    /// @details Must be called while the thread is not running; calling it on a
    /// running thread faults. The handler is copied when the thread starts.
    /// ExitThread() from another thread returns after it completes. If the thread
    /// exits itself (ExitThread() from a handler on this thread), it runs after the
    /// owning ThreadXThread may already be destroyed, so it must not touch that object.
    void SetExitHandler(const dmq::UnicastDelegate<void()>& handler) { DMQ_ASSERT_TRUE(m_thread.tx_thread_id == 0); m_exitHandler = handler; }
    void SetExitHandler(dmq::UnicastDelegate<void()>&& handler) { DMQ_ASSERT_TRUE(m_thread.tx_thread_id == 0); m_exitHandler = std::move(handler); }

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
        DMQ_ASSERT_TRUE(m_thread.tx_thread_id == 0);
        DMQ_ASSERT_TRUE(interval > dmq::Duration::zero());
        m_idleHandler = handler;
        m_idleInterval = interval;
    }
    void SetIdleHandler(dmq::UnicastDelegate<void()>&& handler, dmq::Duration interval = dmq::THREAD_IDLE_INTERVAL)
    {
        DMQ_ASSERT_TRUE(m_thread.tx_thread_id == 0);
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
    ThreadXThread(const ThreadXThread&) = delete;
    ThreadXThread& operator=(const ThreadXThread&) = delete;

    /// Entry point for the thread
    static void Process(ULONG instance);

    // Run loop called by Process
    void Run();

    /// @brief Registry mapping a ThreadX TX_THREAD control block back to its
    /// owning ThreadXThread instance.
    /// @details ThreadX's tx_thread_create() entry_input parameter is a
    /// ULONG, which cannot safely carry a 64-bit 'this' pointer on hosts
    /// where ULONG is 32 bits (e.g. this Linux/GNU simulation port) --
    /// reinterpret_cast<ULONG>(this) would truncate it. Process() instead
    /// looks up 'this' via tx_thread_identify() against this registry.
    /// Entries are added before tx_thread_resume() is called, so Process()
    /// can never observe a not-yet-registered thread.
    static dmq::xmap<TX_THREAD*, ThreadXThread*>& GetInstanceRegistry();
    static dmq::RecursiveMutex& GetInstanceRegistryLock();

    /// Check watchdog is expired. Called by WatchdogCheckAll().
    void WatchdogCheck();

    /// Get registry head using the "Immortal" Pattern
    static ThreadXThread*& GetWatchdogHead();

    /// Get registry lock using the "Immortal" Pattern
    static dmq::RecursiveMutex& GetWatchdogLock();

    const dmq::xstring THREAD_NAME;
    const dmq::xstring CPU_NAME;
    const size_t m_queueSize; // Stored queue size
    const FullPolicy FULL_POLICY;
    const dmq::Duration m_dispatchTimeout;
    UINT m_priority;    // Stored priority

    // ThreadX Control Blocks
    TX_THREAD m_thread;
    ThreadXDelegateQueue m_queue;

    // Optional handler invoked when a message is dropped (FullPolicy::DROP or TIMEOUT)
    dmq::UnicastDelegate<void(size_t)> m_droppedHandler;

    // Optional handlers invoked on the worker thread at start, exit and when idle
    dmq::UnicastDelegate<void()> m_startHandler;
    dmq::UnicastDelegate<void()> m_exitHandler;
    dmq::UnicastDelegate<void()> m_idleHandler;
    dmq::Duration m_idleInterval = dmq::Duration::zero();
    TX_SEMAPHORE m_exitSem; // Semaphore to signal thread completion
    TX_SEMAPHORE m_startSem; // Put by Run() once the start handler has run
    bool m_startSync = false; // CreateThread() waits for the start handler (called from a thread)
    std::atomic<bool> m_exit = false;

    // Optional start gate (SetStartGate()): its state, and whether the thread is
    // held at it (the watchdog doesn't report a thread waiting by design)
    StartGateClient m_startGate;
    std::atomic<bool> m_discard = false; // ExitPolicy::DISCARD: cancel queued messages instead of invoking
    bool* m_selfExitPtr = nullptr;

    // Set when ExitThread() is called from this thread itself (see ExitThread()):
    // the cleanup is then deferred to a later ExitThread() from another thread.
    std::atomic<bool> m_selfExited{false};

    // Stack memory required by ThreadX (Managed by RAII)
    // Using ULONG[] ensures correct alignment for the ThreadX stack
    std::unique_ptr<ULONG[]> m_stackMemory;

    // Configurable stack size (bytes)
    static const ULONG STACK_SIZE = static_cast<ULONG>(dmq::THREADX_STACK_BYTES); // DMQ_THREADX_STACK_BYTES

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime{dmq::TimePoint()};
    std::atomic<dmq::Duration> m_watchdogTimeout{dmq::Duration(0)};
    ThreadXThread* m_watchdogNext = nullptr;

#if defined(DMQ_DATABUS_TOOLS)
    TX_MUTEX m_statMutex; // Mutex to protect statistics
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
/// keeps compiling unchanged against the ThreadX port.
using Thread = ThreadXThread;

} // namespace dmq::os

#endif // _THREAD_THREADX_H
