#ifndef _QT_THREAD_H
#define _QT_THREAD_H

/// @file QtThread.h
/// @brief Qt implementation of the DelegateMQ IThread interface.
///
/// @note This implementation is a basic port. For reference, the stdlib and win32
/// implementations provide additional features:
/// 1. Priority Support: Uses a priority queue to respect dmq::Priority.
/// 2. Synchronized Startup: CreateThread() blocks until the worker thread is ready.
///
/// **Key Features:**
/// * **QThread Integration:** Wraps `QThread` and uses a Worker object to execute
///   delegates in the target thread's event loop.
/// * **FullPolicy Support:** Configurable back-pressure (DROP, FAULT, or TIMEOUT) using
///   `QMutex` and `QWaitCondition`.
/// * **Signal/Slot Dispatch:** Uses Qt's meta-object system to bridge delegate
///   execution across thread boundaries.
/// * **Watchdog Integration:** Optional heartbeat mechanism detects stalled or deadlocked
///   threads. Enable by passing a timeout to CreateThread(). Requires
///   Timer::ProcessTimers() to be called from a context that can preempt watched threads
///   -- typically a hardware timer ISR or the highest-priority task in the system.
///

#include "delegate/IThread.h"
#include "delegate/UnicastDelegate.h"
#include <QThread>
#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QSemaphore>
#include <QTimer>
#include <memory>
#include <atomic>
#include <optional>
#include <string>

namespace dmq::os {

/// @brief Policy applied when the thread message queue is full. See dmq::FullPolicy
/// in DelegateOpt.h for the canonical definition, shared by every dmq::os::Thread port.
using FullPolicy = dmq::FullPolicy;

/// @brief What ExitThread() does with queued messages. See dmq::ExitPolicy in DelegateOpt.h.
using ExitPolicy = dmq::ExitPolicy;

// ----------------------------------------------------------------------------
// Worker Object
// Lives on the target QThread and executes the slots
// ----------------------------------------------------------------------------
class QtThread;
class Worker : public QObject
{
    Q_OBJECT
public:
    Worker(QtThread* thread = nullptr) : m_thread(thread) {}
    void ClearThread() { m_thread = nullptr; }

    /// Start the idle countdown. Must be called on the worker's thread.
    void StartIdleTimer(const dmq::UnicastDelegate<void()>& handler, dmq::Duration interval);

    /// Register `thread` for dmq::ThisThread::GetCurrent() until EndCurrentScope().
    /// Both must be called on the worker's thread.
    void BeginCurrentScope(dmq::IThread* thread) { m_currentScope.emplace(thread); }
    void EndCurrentScope() { m_currentScope.reset(); }

    /// ExitPolicy::DISCARD: cancel messages instead of invoking them. Set by
    /// ExitThread() from any thread before it queues the event loop's quit.
    void SetDiscard(bool discard) { m_discard.store(discard); }

public slots:
    void OnDispatch(std::shared_ptr<dmq::DelegateMsg> msg);

signals:
    void MessageProcessed();

private:
    QtThread* m_thread;

    // Idle handler copy and its single-shot timer, restarted by every message.
    // Created on the worker's thread by StartIdleTimer(); null if no idle handler.
    dmq::UnicastDelegate<void()> m_idleHandler;
    QTimer* m_idleTimer = nullptr;

    // Held for the life of the event loop: a Qt worker has no loop stack frame
    std::optional<dmq::CurrentThreadScope> m_currentScope;

    std::atomic<bool> m_discard{ false };
};

class QtThread : public QObject, public dmq::IThread
{
    Q_OBJECT

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
    /// @param threadName Name for debugging (QObject::objectName)
    /// @param maxQueueSize Max number of messages in queue (0 = Default dmq::DEFAULT_QUEUE_SIZE)
    /// @param fullPolicy Action when queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    /// @param cpuName Optional CPU/Core name grouping for monitoring tools.
    QtThread(const char* threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const char* cpuName = "");

    QtThread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT, const std::string& cpuName = "")
        : QtThread(threadName.c_str(), maxQueueSize, fullPolicy, dispatchTimeout, cpuName.c_str()) {}

    /// Destructor
    ~QtThread();

    /// Create and start the internal QThread. If watchdogTimeout value
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

    /// Get the QThread pointer (used as the ID)
    QThread* GetThreadId();

    /// Get the current executing QThread pointer
    static QThread* GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    dmq::xstring GetThreadName() const { return m_threadName; }

    /// Get current queue size
    size_t GetQueueSize() const { return m_queueSize.load(); }

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
    void SetStartHandler(const dmq::UnicastDelegate<void()>& handler) { DMQ_ASSERT_TRUE(!m_thread); m_startHandler = handler; }
    void SetStartHandler(dmq::UnicastDelegate<void()>&& handler) { DMQ_ASSERT_TRUE(!m_thread); m_startHandler = std::move(handler); }

    /// @brief Register a handler invoked on the worker thread after it processes
    /// its last message, as the thread exits. Use it to undo SetStartHandler() setup.
    /// @details Must be called while the thread is not running; calling it on a
    /// running thread faults. The handler is copied when the thread starts.
    /// ExitThread() from another thread returns after it completes. If the thread
    /// exits itself (ExitThread() from a handler on this thread), it runs after the
    /// owning QtThread may already be destroyed, so it must not touch that object.
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
    /// @details The Run() loop refreshes the timestamp automatically on every iteration.
    /// Call this from inside long-running message handlers to prevent a false watchdog
    /// alarm when a handler legitimately takes longer than watchdogTimeout.
    void ThreadCheck();

    /// @brief Static method to check all registered threads for watchdog expiration.
    static void WatchdogCheckAll();

#if defined(DMQ_DATABUS_TOOLS)
    /// @brief Capture and reset windowed statistics.
    ThreadStats SnapshotStats();

    /// @brief Internal method for Worker to update invoke stats.
    void UpdateInvokeStats(dmq::Duration invokeTime);
#endif

signals:
    // Internal signal to bridge threads
    void SignalDispatch(std::shared_ptr<dmq::DelegateMsg> msg);

private slots:
    void OnMessageProcessed() { 
        m_queueSize--; 
        m_cvNotFull.wakeAll();
    }

private:
    QtThread(const QtThread&) = delete;
    QtThread& operator=(const QtThread&) = delete;

    /// Check watchdog is expired. Called from Timer::ProcessTimers() context.
    void WatchdogCheck();

    /// Get registry head using the "Immortal" Pattern
    static QtThread*& GetWatchdogHead();

    /// Get registry lock using the "Immortal" Pattern
    static dmq::RecursiveMutex& GetWatchdogLock();

    const dmq::xstring m_threadName;
    const dmq::xstring m_cpuName;
    const size_t m_maxQueueSize;
    const FullPolicy m_fullPolicy;
    const dmq::Duration m_dispatchTimeout;
    QThread* m_thread = nullptr;
    Worker* m_worker = nullptr;
    std::atomic<size_t> m_queueSize{0};
    QMutex m_mutex;
    QWaitCondition m_cvNotFull;

    // Released on the worker thread once the start handler has run
    QSemaphore m_startSem;

    // Set by ExitThread(): DispatchDelegate() rejects new messages, which would
    // otherwise queue behind the event loop's quit and never run
    std::atomic<bool> m_exiting{ false };

    // Optional handler invoked when a message is dropped (FullPolicy::DROP or TIMEOUT)
    dmq::UnicastDelegate<void(size_t)> m_droppedHandler;

    // Optional handlers invoked on the worker thread at start, exit and when idle
    dmq::UnicastDelegate<void()> m_startHandler;
    dmq::UnicastDelegate<void()> m_exitHandler;
    dmq::UnicastDelegate<void()> m_idleHandler;
    dmq::Duration m_idleInterval = dmq::Duration::zero();

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::atomic<dmq::Duration> m_watchdogTimeout;
    QtThread* m_watchdogNext = nullptr;

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
/// keeps compiling unchanged against the Qt port.
using Thread = QtThread;

} // namespace dmq::os

#endif // _QT_THREAD_H
