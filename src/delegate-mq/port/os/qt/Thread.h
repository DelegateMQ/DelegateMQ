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
///   — typically a hardware timer ISR or the highest-priority task in the system.
///
#include "delegate/IThread.h"
#include "extras/util/Timer.h"
#include <QThread>
#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <memory>
#include <string>
#include <optional>

// Ensure DelegateMsg is known to Qt MetaType system
Q_DECLARE_METATYPE(std::shared_ptr<dmq::DelegateMsg>)

namespace dmq::os {

class Worker;

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

class Thread : public QObject, public dmq::IThread
{
    Q_OBJECT

public:
    /// Default queue size if 0 is passed
    static const size_t DEFAULT_QUEUE_SIZE = 20;

    /// Constructor
    /// @param threadName Name for debugging (QObject::objectName)
    /// @param maxQueueSize Max number of messages in queue (0 = Default 20)
    /// @param fullPolicy Action when queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    Thread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT);

    /// Destructor
    ~Thread();

    /// Create and start the internal QThread. If watchdogTimeout value
    /// provided, the maximum watchdog interval is used. Otherwise no watchdog.
    /// @param[in] watchdogTimeout - optional watchdog timeout.
    /// @return TRUE if thread is created. FALSE otherwise.
    bool CreateThread(std::optional<dmq::Duration> watchdogTimeout = std::nullopt);

    /// Stop the QThread
    void ExitThread();

    /// Get the QThread pointer (used as the ID)
    QThread* GetThreadId();

    /// Get the current executing QThread pointer
    static QThread* GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    std::string GetThreadName() const { return m_threadName; }

    /// Get current queue size
    size_t GetQueueSize() const { return m_queueSize.load(); }

    /// Sleep for a duration.
    /// @param[in] timeout - the duration to sleep.
    static void Sleep(dmq::Duration timeout);

    // IThread Interface Implementation
    virtual bool DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg) override;

signals:
    // Internal signal to bridge threads
    void SignalDispatch(std::shared_ptr<dmq::DelegateMsg> msg);

private slots:
    void OnMessageProcessed() { 
        m_queueSize--; 
        m_cvNotFull.wakeAll();
    }

private:
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    /// Check watchdog is expired. Called from Timer::ProcessTimers() context.
    void WatchdogCheck();

    /// Timer expiration function dispatched to this thread to update m_lastAliveTime.
    void ThreadCheck();

    const std::string m_threadName;
    const size_t m_maxQueueSize;
    const FullPolicy m_fullPolicy;
    const dmq::Duration m_dispatchTimeout;
    QThread* m_thread = nullptr;
    Worker* m_worker = nullptr;
    std::atomic<size_t> m_queueSize{0};
    QMutex m_mutex;
    QWaitCondition m_cvNotFull;

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::unique_ptr<Timer> m_watchdogTimer;
    dmq::ScopedConnection m_watchdogTimerConn;
    std::unique_ptr<Timer> m_threadTimer;
    dmq::ScopedConnection m_threadTimerConn;
    std::atomic<dmq::Duration> m_watchdogTimeout;
};

// ----------------------------------------------------------------------------
// Worker Object
// Lives on the target QThread and executes the slots
// ----------------------------------------------------------------------------
class Worker : public QObject
{
    Q_OBJECT
signals:
    void MessageProcessed();

public slots:
    void OnDispatch(std::shared_ptr<dmq::DelegateMsg> msg)
    {
        if (msg) {
            auto invoker = msg->GetInvoker();
            if (invoker) {
                invoker->Invoke(msg);
            }
        }
        emit MessageProcessed();
    }
};

} // namespace dmq::os

#endif // _QT_THREAD_H