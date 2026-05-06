#ifndef DMQ_THREAD_QT
#error "port/os/qt/Thread.cpp requires DMQ_THREAD_QT. Remove this file from your build configuration or define DMQ_THREAD_QT."
#endif

#include "DelegateMQ.h"
#include "Thread.h"
#include "extras/util/Fault.h"
#include <QDebug>

namespace dmq::os {

using namespace dmq;
using namespace dmq::util;

// Define ASSERT_TRUE if not already defined
#ifndef ASSERT_TRUE
#define ASSERT_TRUE(x) Q_ASSERT(x)
#endif

// Register the metatype ID once
static int registerId = qRegisterMetaType<std::shared_ptr<dmq::DelegateMsg>>();

//----------------------------------------------------------------------------
// Worker::OnDispatch
//----------------------------------------------------------------------------
void Worker::OnDispatch(std::shared_ptr<dmq::DelegateMsg> msg) {
    if (msg) {
        auto invoker = msg->GetInvoker();
        if (invoker) {
#if defined(DMQ_DATABUS_TOOLS)
            dmq::TimePoint start = Timer::GetNow();
#endif
            invoker->Invoke(msg);
#if defined(DMQ_DATABUS_TOOLS)
            if (m_thread) {
                dmq::Duration invokeTime = Timer::GetNow() - start;
                m_thread->UpdateInvokeStats(invokeTime);
            }
#endif
        }
    }
    emit MessageProcessed();
}

//----------------------------------------------------------------------------
// Thread Constructor
//----------------------------------------------------------------------------
Thread::Thread(const std::string& threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout, const std::string& cpuName)
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
Thread::~Thread()
{
    ExitThread();

    const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
    Thread** pp = &GetWatchdogHead();
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
bool Thread::CreateThread(std::optional<dmq::Duration> watchdogTimeout)
{
    if (!m_thread)
    {
        m_thread = new QThread();
        m_thread->setObjectName(QString::fromStdString(m_threadName));

        // Create worker and move it to the new thread
        m_worker = new Worker(this);
        m_worker->moveToThread(m_thread);

        // Connect the Dispatch signal to the Worker's slot.
        // Qt::QueuedConnection is mandatory for cross-thread communication,
        // but Qt defaults to AutoConnection which handles this correctly.
        connect(this, &Thread::SignalDispatch, 
                m_worker, &Worker::OnDispatch, 
                Qt::QueuedConnection);

        // Track when message is processed to decrement m_queueSize
        connect(m_worker, &Worker::MessageProcessed,
                this, &Thread::OnMessageProcessed);

        // Ensure worker is deleted when thread finishes
        connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        
        // Also delete the QThread object itself when finished (optional, depending on ownership)
        // connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

        m_thread->start();

        m_lastAliveTime.store(Timer::GetNow());

        if (watchdogTimeout.has_value())
        {
            m_watchdogTimeout = watchdogTimeout.value();

            m_watchdogTimer = std::unique_ptr<Timer>(new Timer());
            m_watchdogTimerConn = m_watchdogTimer->OnExpired.Connect(
                MakeDelegate(this, &Thread::WatchdogCheck));
            m_watchdogTimer->Start(m_watchdogTimeout.load() / 2);

            // Add this thread to the watchdog list
            {
                dmq::LockGuard<dmq::RecursiveMutex> lock(GetWatchdogLock());
                m_watchdogNext = GetWatchdogHead();
                GetWatchdogHead() = this;
            }
        }
    }
    return true;
}

//----------------------------------------------------------------------------
// WatchdogCheck
//----------------------------------------------------------------------------
void Thread::WatchdogCheck()
{
    auto now = Timer::GetNow();
    auto lastAlive = m_lastAliveTime.load();
    auto delta = now - lastAlive;
    if (delta > m_watchdogTimeout.load())
    {
        WatchdogHandler(m_threadName.c_str());
    }
}

//----------------------------------------------------------------------------
// ThreadCheck
//----------------------------------------------------------------------------
void Thread::ThreadCheck()
{
    m_lastAliveTime.store(Timer::GetNow());
}

//----------------------------------------------------------------------------
// WatchdogCheckAll
//----------------------------------------------------------------------------
void Thread::WatchdogCheckAll()
{
    const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
    Thread* p = GetWatchdogHead();
    while (p != nullptr)
    {
        p->WatchdogCheck();
        p = p->m_watchdogNext;
    }
}

//----------------------------------------------------------------------------
// GetWatchdogHead
//----------------------------------------------------------------------------
Thread*& Thread::GetWatchdogHead()
{
    static Thread* head = nullptr;
    return head;
}

//----------------------------------------------------------------------------
// GetWatchdogLock
//----------------------------------------------------------------------------
dmq::RecursiveMutex& Thread::GetWatchdogLock()
{
    static dmq::RecursiveMutex* lock = new dmq::RecursiveMutex();
    return *lock;
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void Thread::ExitThread()
{
    if (m_thread)
    {
        if (m_watchdogTimer)
        {
            m_watchdogTimer->Stop();
            m_watchdogTimerConn.Disconnect();
        }
        m_thread->quit();

        // Wake any blocked threads
        m_mutex.lock();
        m_cvNotFull.wakeAll();
        m_mutex.unlock();

        m_thread->wait();
        
        // Cleanup manually if not using deleteLater
        delete m_thread; 
        m_thread = nullptr;
        
        // Worker is usually deleted by deleteLater, but we can force null here
        m_worker = nullptr;
    }
}

//----------------------------------------------------------------------------
// GetThreadId
//----------------------------------------------------------------------------
QThread* Thread::GetThreadId()
{
    return m_thread;
}

//----------------------------------------------------------------------------
// GetCurrentThreadId
//----------------------------------------------------------------------------
QThread* Thread::GetCurrentThreadId()
{
    return QThread::currentThread();
}

//----------------------------------------------------------------------------
// IsCurrentThread
//----------------------------------------------------------------------------
bool Thread::IsCurrentThread()
{
    return GetThreadId() == GetCurrentThreadId();
}

void Thread::Sleep(dmq::Duration timeout) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    QThread::msleep(static_cast<unsigned long>(ms));
}

//----------------------------------------------------------------------------
// DispatchDelegate
//----------------------------------------------------------------------------
bool Thread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    // Safety check: Don't emit if thread is tearing down
    if (m_thread && m_thread->isRunning()) 
    {
        m_mutex.lock();
        if (m_queueSize >= m_maxQueueSize)
        {
            if (m_fullPolicy == FullPolicy::DROP)
            {
                m_mutex.unlock();
                return false; // silently discard
            }

            if (m_fullPolicy == FullPolicy::FAULT)
            {
                m_mutex.unlock();
                printf("[Thread] CRITICAL: Queue full on thread '%s'! TRIGGERING FAULT.\n", m_threadName.c_str());
                ASSERT_TRUE(m_queueSize < m_maxQueueSize);
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
                        m_mutex.unlock();
                        printf("[Thread] WARNING: Queue post timed out on '%s' — possible deadlock. Message dropped.\n", m_threadName.c_str());
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
void Thread::UpdateInvokeStats(dmq::Duration invokeTime)
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
Thread::ThreadStats Thread::SnapshotStats()
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
        stats.invoke_avg_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_invokeTotalWindow).count() / (m_invokeCountWindow * 1000.0f);
    } else {
        stats.invoke_avg_ms = 0.0f;
    }

    stats.invoke_max_window_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_invokeMaxWindow).count() / 1000.0f;
    stats.invoke_max_all_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_invokeMaxAll).count() / 1000.0f;

    stats.dispatch_count = m_dispatchCountAll;

    // Reset windowed stats
    m_queueDepthMaxWindow = stats.queue_depth;
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
