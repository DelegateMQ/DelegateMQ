// Compiles to nothing unless DMQ_THREAD_QT is selected, so every DelegateMQ source
// file can be added to a build (see src/DelegateMQ.cpp).
#include "delegate/DelegateSelect.h"
#if defined(DMQ_THREAD_QT)

#include "DelegateMQ.h"
#include "QtThread.h"
#include "extras/util/Fault.h"
#include <QDebug>
#include <limits>

namespace dmq::os {

using namespace dmq;
using namespace dmq::util;

// Define DMQ_ASSERT_TRUE if not already defined
#ifndef DMQ_ASSERT_TRUE
#define DMQ_ASSERT_TRUE(x) Q_ASSERT(x)
#endif

// QTimer takes an int millisecond interval: at least 1, clamped so a very long
// interval (over ~24 days) doesn't overflow
static int ToTimerInterval(long long ms)
{
    if (ms < 1) return 1;
    if (ms > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
    return static_cast<int>(ms);
}

// Register the metatype ID once
static int registerId = qRegisterMetaType<std::shared_ptr<dmq::DelegateMsg>>();

//----------------------------------------------------------------------------
// Worker::OnDispatch
//----------------------------------------------------------------------------
void Worker::OnDispatch(std::shared_ptr<dmq::DelegateMsg> msg) {
    // Watchdog heartbeat: the event loop reached another message
    if (m_thread)
        m_thread->ThreadCheck();

    // ExitPolicy::DISCARD: skip messages queued ahead of the event loop's quit
    if (m_discard.load()) {
        if (msg)
            msg->Cancel();
        emit MessageProcessed();
        return;
    }

    if (msg) {
        auto invoker = msg->GetInvoker();
        if (invoker) {
#if defined(DMQ_DATABUS_TOOLS)
            dmq::TimePoint start = Timer::GetNow();
#endif
#if defined(__cpp_exceptions) && !defined(DMQ_ASSERTS)
            try {
                bool success = invoker->Invoke(msg);
                DMQ_ASSERT_TRUE(success);
            }
            catch (const std::bad_alloc& e) {
                qWarning() << "[Thread:" << m_thread->objectName() << "] Unhandled bad_alloc in delegate callback:" << e.what();
                DMQ_ASSERT();
            }
            catch (const std::invalid_argument& e) {
                qWarning() << "[Thread:" << m_thread->objectName() << "] Unhandled invalid_argument in delegate callback:" << e.what();
                DMQ_ASSERT();
            }
            catch (const std::runtime_error& e) {
                qWarning() << "[Thread:" << m_thread->objectName() << "] Unhandled runtime_error in delegate callback:" << e.what();
                DMQ_ASSERT();
            }
            catch (const std::exception& e) {
                qWarning() << "[Thread:" << m_thread->objectName() << "] Unhandled exception in delegate callback:" << e.what();
                DMQ_ASSERT();
            }
            catch (...) {
                qWarning() << "[Thread:" << m_thread->objectName() << "] Unhandled unknown exception in delegate callback.";
                DMQ_ASSERT();
            }
#else
            bool success = invoker->Invoke(msg);
            DMQ_ASSERT_TRUE(success);
#endif
#if defined(DMQ_DATABUS_TOOLS)
            if (m_thread) {
                dmq::Duration invokeTime = Timer::GetNow() - start;
                m_thread->UpdateInvokeStats(invokeTime);
            }
#endif
        }
    }

    // Any message restarts the idle countdown
    if (m_idleTimer)
        m_idleTimer->start();

    emit MessageProcessed();
}

//----------------------------------------------------------------------------
// Worker::StartIdleTimer
//----------------------------------------------------------------------------
void Worker::StartIdleTimer(const dmq::UnicastDelegate<void()>& handler, dmq::Duration interval)
{
    m_idleHandler = handler;
    auto ms = std::chrono::ceil<std::chrono::milliseconds>(interval).count();

    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(ToTimerInterval(ms));
    connect(m_idleTimer, &QTimer::timeout, this, [this]() {
        m_idleHandler();
        m_idleTimer->start();
    });
    m_idleTimer->start();
}

//----------------------------------------------------------------------------
// Worker::StartHeartbeat
//----------------------------------------------------------------------------
void Worker::StartHeartbeat(dmq::Duration interval)
{
    auto ms = std::chrono::ceil<std::chrono::milliseconds>(interval).count();

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(ToTimerInterval(ms));
    // Fires only while the event loop runs, so a stuck handler stops the heartbeat
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        if (m_thread)
            m_thread->ThreadCheck();
    });
    m_heartbeatTimer->start();
}

//----------------------------------------------------------------------------
// Thread Constructor
//----------------------------------------------------------------------------
QtThread::QtThread(const char* threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout, const char* cpuName)
    : m_threadName(threadName)
    , m_cpuName(cpuName)
    , m_maxQueueSize((maxQueueSize == 0) ? DEFAULT_QUEUE_SIZE : maxQueueSize)
    , m_fullPolicy(fullPolicy)
    , m_dispatchTimeout(dispatchTimeout)
{
}

//----------------------------------------------------------------------------
// Thread Destructor
//----------------------------------------------------------------------------
QtThread::~QtThread()
{
    ExitThread();

    const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
    QtThread** pp = &GetWatchdogHead();
    while (*pp != nullptr)
    {
        if (*pp == this)
        {
            *pp = this->m_watchdogNext;
            this->m_watchdogNext = nullptr;
            break;
        }
        pp = &((*pp)->m_watchdogNext);
    }
}

//----------------------------------------------------------------------------
// CreateThread
//----------------------------------------------------------------------------
bool QtThread::CreateThread(std::optional<dmq::Duration> watchdogTimeout)
{
    if (!m_thread)
    {
        m_exiting.store(false);
        // Held from creation, so the watchdog never sees a gated thread as hung
        // before it reaches the gate (e.g. created before the scheduler runs)
        m_startGate.OnCreate();
        // Set before the thread exists: its loop reads the timeout before its
        // first queue wait, and an idle thread waits forever with no timeout
        m_lastAliveTime.store(Timer::GetNow());
        m_watchdogTimeout.store(watchdogTimeout.value_or(dmq::Duration(0)));
        m_thread = new QThread();
        m_thread->setObjectName(QString::fromUtf8(m_threadName.c_str()));

        // Create worker and move it to the new thread
        m_worker = new Worker(this);
        m_worker->moveToThread(m_thread);

        // Connect the Dispatch signal to the Worker's slot.
        // Qt::QueuedConnection is mandatory for cross-thread communication,
        // but Qt defaults to AutoConnection which handles this correctly.
        connect(this, &QtThread::SignalDispatch, 
                m_worker, &Worker::OnDispatch, 
                Qt::QueuedConnection);

        // Track when message is processed to decrement m_queueSize
        connect(m_worker, &Worker::MessageProcessed,
                this, &QtThread::OnMessageProcessed);

        // Ensure worker is deleted when thread finishes
        connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

        // Start handler and idle timer run on the new thread (started is emitted
        // there; DirectConnection runs the lambda in place) before its event loop
        // processes any dispatched message.
        Worker* worker = m_worker;
        QSemaphore* startSem = &m_startSem;
        dmq::IThread* self = this;
        QtThread* qt = this;    // start gate state; see the self-exit check below
        connect(m_thread, &QThread::started, m_worker,
            [worker, self, qt, startSem, startHandler = m_startHandler,
             idleHandler = m_idleHandler, idleInterval = m_idleInterval,
             watchdog = m_watchdogTimeout.load()]() {
                worker->BeginCurrentScope(self);
                if (startHandler)
                    startHandler();
                if (idleHandler)
                    worker->StartIdleTimer(idleHandler, idleInterval);
                if (watchdog.count() > 0)
                    worker->StartHeartbeat(watchdog / dmq::WATCHDOG_HEARTBEAT_DIVISOR);

                // A start handler that exited this thread (ExitThread() on itself)
                // leaves right after the release below: once CreateThread() returns,
                // the QtThread may be destroyed, so nothing after it may touch 'qt'.
                const bool exitedInStart = qt->m_exiting.load();
                startSem->release();
                if (exitedInStart)
                    return;

                // Optional start gate: hold here, before the event loop runs any
                // dispatched message, until it opens
                if (qt->m_startGate.Enabled())
                {
                    const bool opened = qt->m_startGate.Wait(qt->m_exiting);
                    qt->m_lastAliveTime.store(Timer::GetNow());   // before Passed()
                    qt->m_startGate.Passed();
                    // ExitThread() while held: the event loop cancels every
                    // queued message (discard) up to the quit queued behind them
                    if (!opened)
                        worker->SetDiscard(true);
                }
            }, Qt::DirectConnection);

        // Exit handler runs on the worker thread as it finishes (finished is
        // emitted there), after GetCurrent() is cleared. Captured by copy so it
        // works even if this QtThread was destroyed by a self-exit.
        connect(m_thread, &QThread::finished, m_worker,
            [worker, exitHandler = m_exitHandler]() {
                worker->EndCurrentScope();
                if (exitHandler)
                    exitHandler();
            }, Qt::DirectConnection);
        
        // Also delete the QThread object itself when finished (optional, depending on ownership)
        // connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

        m_thread->start();

        // Wait for the start handler to complete
        m_startSem.acquire();

        m_lastAliveTime.store(Timer::GetNow());

        if (watchdogTimeout.has_value())
        {

            // Add to watchdog registry if not already present
            {
                dmq::LockGuard<dmq::RecursiveMutex> lock(GetWatchdogLock());
                bool found = false;
                QtThread* p = GetWatchdogHead();
                while (p != nullptr)
                {
                    if (p == this)
                    {
                        found = true;
                        break;
                    }
                    p = p->m_watchdogNext;
                }
                if (!found)
                {
                    m_watchdogNext = GetWatchdogHead();
                    GetWatchdogHead() = this;
                }
            }
        }
    }
    return true;
}

//----------------------------------------------------------------------------
// WatchdogCheck
//----------------------------------------------------------------------------
void QtThread::WatchdogCheck()
{
    if (m_startGate.IsHeld())
        return;     // held at its start gate by design, not hung

    auto now = Timer::GetNow();
    auto lastAlive = m_lastAliveTime.load();
    auto watchdogTimeout = m_watchdogTimeout.load();

    if (watchdogTimeout.count() > 0)
    {
        auto delta = now - lastAlive;
        if (delta > watchdogTimeout)
        {
            WatchdogHandler(m_threadName.c_str());
        }
    }
}

//----------------------------------------------------------------------------
// ThreadCheck
//----------------------------------------------------------------------------
void QtThread::ThreadCheck()
{
    m_lastAliveTime.store(Timer::GetNow());
}

//----------------------------------------------------------------------------
// WatchdogCheckAll
//----------------------------------------------------------------------------
void QtThread::WatchdogCheckAll()
{
    const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
    QtThread* p = GetWatchdogHead();
    while (p != nullptr)
    {
        p->WatchdogCheck();
        p = p->m_watchdogNext;
    }
}

//----------------------------------------------------------------------------
// GetWatchdogHead
//----------------------------------------------------------------------------
QtThread*& QtThread::GetWatchdogHead()
{
    static QtThread* head = nullptr;
    return head;
}

//----------------------------------------------------------------------------
// GetWatchdogLock
//----------------------------------------------------------------------------
dmq::RecursiveMutex& QtThread::GetWatchdogLock()
{
    static dmq::RecursiveMutex* lock = new dmq::RecursiveMutex();
    return *lock;
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void QtThread::ExitThread(ExitPolicy policy)
{
    if (m_thread)
    {
        m_exiting.store(true);
        const bool selfExit = (QThread::currentThread() == m_thread);
        if (selfExit)
            m_startGate.Leave();     // exited from its start handler: never reaches the gate

        // A self-exit always discards: the event loop keeps running after this
        // object may be destroyed.
        m_worker->SetDiscard(policy == ExitPolicy::DISCARD || selfExit);

        // A thread held at its start gate isn't waiting on its queue: wake it so it
        // sees the exit flag (set above) and leaves. Never touches the gate itself,
        // which may be destroyed once the thread is released.
        m_startGate.Wake();

        // Quit queued behind every message already dispatched, so DRAIN invokes
        // them first and DISCARD cancels them first.
        QThread* thread = m_thread;
        QMetaObject::invokeMethod(m_worker, [thread]() { thread->quit(); }, Qt::QueuedConnection);

        // Wake any blocked threads
        m_mutex.lock();
        m_cvNotFull.wakeAll();
        m_mutex.unlock();

        if (!selfExit) {
            m_thread->wait();
            delete m_thread;
        } else {
            m_thread->deleteLater();
        }

        // The thread has finished (or, on a self-exit, is in its last handler):
        // stop watchdog checks. Not earlier, so a thread that hangs while
        // ExitThread() waits for it is still reported.
        m_watchdogTimeout.store(dmq::Duration(0));

        // Worker thread has fully stopped; null the back-pointer so the Worker
        // cannot access this Thread if it is kept alive by deleteLater.
        if (m_worker) {
            m_worker->ClearThread();
        }

        m_thread = nullptr;
        m_worker = nullptr;
    }
}

//----------------------------------------------------------------------------
// GetThreadId
//----------------------------------------------------------------------------
QThread* QtThread::GetThreadId()
{
    return m_thread;
}

//----------------------------------------------------------------------------
// GetCurrentThreadId
//----------------------------------------------------------------------------
QThread* QtThread::GetCurrentThreadId()
{
    return QThread::currentThread();
}

//----------------------------------------------------------------------------
// IsCurrentThread
//----------------------------------------------------------------------------
bool QtThread::IsCurrentThread()
{
    return GetThreadId() == GetCurrentThreadId();
}

void QtThread::Sleep(dmq::Duration timeout) {
    dmq::ThisThread::sleep_for(timeout);
}

//----------------------------------------------------------------------------
// DispatchDelegate
//----------------------------------------------------------------------------
bool QtThread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    // Safety check: Don't emit if thread is tearing down
    if (m_thread && m_thread->isRunning() && !m_exiting.load())
    {
        m_mutex.lock();
        if (m_queueSize >= m_maxQueueSize)
        {
            if (m_fullPolicy == FullPolicy::DROP)
            {
                size_t depth = m_queueSize.load();
                m_mutex.unlock();
                if (m_droppedHandler)
                    m_droppedHandler(depth);
                return false; // silently discard
            }

            if (m_fullPolicy == FullPolicy::FAULT)
            {
                m_mutex.unlock();
                printf("[Thread] CRITICAL: Queue full on thread '%s'! TRIGGERING FAULT.\n", m_threadName.c_str());
                DMQ_ASSERT_TRUE(m_queueSize < m_maxQueueSize);
                return false;
            }

            if (m_fullPolicy == FullPolicy::TIMEOUT)
            {
                auto ms = static_cast<unsigned long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(m_dispatchTimeout).count());
                while (m_queueSize >= m_maxQueueSize && m_thread->isRunning())
                {
                    if (!m_cvNotFull.wait(&m_mutex, ms))
                    {
                        size_t depth = m_queueSize.load();
                        m_mutex.unlock();
                        printf("[Thread] WARNING: Queue post timed out on '%s' — possible deadlock. Message dropped.\n", m_threadName.c_str());
                        if (m_droppedHandler)
                            m_droppedHandler(depth);
                        return false;
                    }
                }
            }
        }

        // Re-check running status after wait
        if (m_thread->isRunning())
        {
            m_queueSize++;
            
#if defined(DMQ_DATABUS_TOOLS)
            // Update monitoring stats
            size_t currentDepth = m_queueSize.load();
            if (currentDepth > m_queueDepthMaxWindow) m_queueDepthMaxWindow = currentDepth;
            if (currentDepth > m_queueDepthMaxAll) m_queueDepthMaxAll = currentDepth;
            m_dispatchCountAll++;
#endif

            // Unlike the other thread ports, msg->GetPriority() is ignored here:
            // the queued signal goes into Qt's single FIFO event queue, so a
            // Priority::HIGH message runs after every message dispatched before
            // it. The other ports keep a HIGH and a NORMAL queue and drain HIGH
            // first. Supporting priority would need a two-lane queue here, with
            // the signal only waking the worker to drain it.
            emit SignalDispatch(msg);
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
    }
    return false;
}

#if defined(DMQ_DATABUS_TOOLS)
//----------------------------------------------------------------------------
// UpdateInvokeStats
//----------------------------------------------------------------------------
void QtThread::UpdateInvokeStats(dmq::Duration invokeTime)
{
    m_mutex.lock();
    m_invokeTotalWindow += invokeTime;
    m_invokeCountWindow++;
    if (invokeTime > m_invokeMaxWindow) m_invokeMaxWindow = invokeTime;
    if (invokeTime > m_invokeMaxAll) m_invokeMaxAll = invokeTime;
    m_mutex.unlock();
}

//----------------------------------------------------------------------------
// SnapshotStats
//----------------------------------------------------------------------------
QtThread::ThreadStats QtThread::SnapshotStats()
{
    m_mutex.lock();
    ThreadStats stats;
    stats.cpu_name = m_cpuName;
    stats.thread_name = m_threadName;
    stats.queue_depth = m_queueSize.load();
    stats.queue_depth_max_window = m_queueDepthMaxWindow;
    stats.queue_depth_max_all = m_queueDepthMaxAll;
    stats.queue_size_limit = m_maxQueueSize;
    
    // Qt port doesn't implement latency yet due to Signal/Slot bridge limitations
    stats.latency_avg_ms = 0.0f;
    stats.latency_max_window_ms = 0.0f;
    stats.latency_max_all_ms = 0.0f;

    if (m_invokeCountWindow > 0) {
        stats.invoke_avg_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_invokeTotalWindow).count() / (static_cast<float>(m_invokeCountWindow) * 1000.0f);
    } else {
        stats.invoke_avg_ms = 0.0f;
    }

    stats.invoke_max_window_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_invokeMaxWindow).count() / 1000.0f;
    stats.invoke_max_all_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_invokeMaxAll).count() / 1000.0f;

    stats.dispatch_count = m_dispatchCountAll;

    // Reset windowed stats
    m_queueDepthMaxWindow = 0;
    m_latencyTotalWindow = Duration(0);
    m_latencyCountWindow = 0;
    m_latencyMaxWindow = Duration(0);

    m_invokeTotalWindow = Duration(0);
    m_invokeCountWindow = 0;
    m_invokeMaxWindow = Duration(0);

    m_mutex.unlock();
    return stats;
}
#endif

} // namespace dmq::os

#endif // DMQ_THREAD_QT
