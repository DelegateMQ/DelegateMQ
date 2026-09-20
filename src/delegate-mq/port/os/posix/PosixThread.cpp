#ifndef DMQ_THREAD_POSIX
#error "port/os/posix/PosixThread.cpp requires DMQ_THREAD_POSIX. Remove this file from your build configuration or define DMQ_THREAD_POSIX."
#endif

#include "DelegateMQ.h"
#include "PosixThread.h"
#include "extras/util/Fault.h"
#include <cerrno>
#include <iostream>

// Thread-local pointer into Process()'s stack frame. Set non-null only while
// Process() is running on a given thread. ExitThread() writes true through it
// when the thread destroys its own owning object so Process() can exit without
// touching freed members.
static thread_local bool* t_self_exit = nullptr;

namespace dmq::os {

using namespace dmq::util;

//----------------------------------------------------------------------------
// MakeAbsTimeout
//----------------------------------------------------------------------------
void PosixThread::MakeAbsTimeout(dmq::Duration timeout, struct timespec& ts)
{
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count();
    ts.tv_sec += static_cast<time_t>(ns / 1000000000LL);
    ts.tv_nsec += static_cast<long>(ns % 1000000000LL);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec += 1;
    }
}

//----------------------------------------------------------------------------
// Thread
//----------------------------------------------------------------------------
PosixThread::PosixThread(const char* threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout, const char* cpuName)
    : THREAD_NAME(threadName)
    , CPU_NAME(cpuName)
    , MAX_QUEUE_SIZE(maxQueueSize == 0 ? dmq::THREAD_DESKTOP_QUEUE_SIZE : maxQueueSize)
    , FULL_POLICY(fullPolicy)
    , m_dispatchTimeout(dispatchTimeout)
    , m_exit(false)
{
    pthread_mutex_init(&m_startMutex, nullptr);
    pthread_cond_init(&m_startCv, nullptr);
    pthread_mutex_init(&m_mutex, nullptr);

    // Both condition variables use CLOCK_MONOTONIC (not the default CLOCK_REALTIME)
    // so pthread_cond_timedwait() deadlines are immune to wall-clock jumps (NTP
    // steps, manual clock changes) -- consistent with dmq::Clock (std::chrono::
    // steady_clock) being what dmq::Duration/dmq::TimePoint are measured against.
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&m_cvNotEmpty, &attr);
    pthread_cond_init(&m_cvNotFull, &attr);
    pthread_condattr_destroy(&attr);
}

//----------------------------------------------------------------------------
// ~Thread
//----------------------------------------------------------------------------
PosixThread::~PosixThread()
{
    ExitThread();

    {
        const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
        PosixThread** pp = &GetWatchdogHead();
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

    pthread_cond_destroy(&m_cvNotFull);
    pthread_cond_destroy(&m_cvNotEmpty);
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_startCv);
    pthread_mutex_destroy(&m_startMutex);
}

//----------------------------------------------------------------------------
// CreateThread
//----------------------------------------------------------------------------
bool PosixThread::CreateThread(std::optional<dmq::Duration> watchdogTimeout)
{
    if (!m_threadCreated)
    {
        m_exit = false;
        m_started = false;

        int rc = pthread_create(&m_thread, nullptr, &PosixThread::ThreadEntry, this);
        if (rc != 0) return false;
        m_threadCreated = true;

        // Set the thread name for debugging (glibc extension, name truncated to
        // 15 chars + NUL -- Linux's TASK_COMM_LEN limit). Not part of the POSIX
        // standard, so this is opportunistic and guarded rather than depended on.
#if defined(__linux__)
        pthread_setname_np(m_thread, THREAD_NAME.substr(0, 15).c_str());
#endif

        // Wait for the thread to enter the Process method
        pthread_mutex_lock(&m_startMutex);
        while (!m_started)
            pthread_cond_wait(&m_startCv, &m_startMutex);
        pthread_mutex_unlock(&m_startMutex);

        m_lastAliveTime.store(Timer::GetNow());

        // Caller wants a watchdog timer?
        if (watchdogTimeout.has_value())
        {
            m_watchdogTimeout = watchdogTimeout.value();

            // Add to watchdog registry if not already present
            {
                dmq::LockGuard<dmq::RecursiveMutex> lock(GetWatchdogLock());
                bool found = false;
                PosixThread* p = GetWatchdogHead();
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
// ThreadEntry
//----------------------------------------------------------------------------
void* PosixThread::ThreadEntry(void* arg)
{
    static_cast<PosixThread*>(arg)->Process();
    return nullptr;
}

//----------------------------------------------------------------------------
// GetThreadId
//----------------------------------------------------------------------------
pthread_t PosixThread::GetThreadId()
{
    if (!m_threadCreated)
        throw std::invalid_argument("Thread pointer is null");

    return m_thread;
}

//----------------------------------------------------------------------------
// GetCurrentThreadId
//----------------------------------------------------------------------------
pthread_t PosixThread::GetCurrentThreadId()
{
    return pthread_self();
}

//----------------------------------------------------------------------------
// IsCurrentThread
//----------------------------------------------------------------------------
bool PosixThread::IsCurrentThread()
{
    if (!m_threadCreated)
        return false;

    return pthread_equal(pthread_self(), m_thread) != 0;
}

//----------------------------------------------------------------------------
// GetQueueSize
//----------------------------------------------------------------------------
size_t PosixThread::GetQueueSize()
{
    pthread_mutex_lock(&m_mutex);
    size_t size = (m_highQueue.size() + m_normalQueue.size());
    pthread_mutex_unlock(&m_mutex);
    return size;
}

void PosixThread::Sleep(dmq::Duration timeout) {
    dmq::ThisThread::sleep_for(timeout);
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void PosixThread::ExitThread()
{
    if (!m_threadCreated) return;

    auto threadMsg = xmake_shared<ThreadMsg>(MSG_EXIT_THREAD, nullptr);

    pthread_mutex_lock(&m_mutex);

    // Set exit flag INSIDE lock before notifying.
    // This ensures that when a blocked producer wakes up, it sees m_exit == true immediately.
    m_exit.store(true);

    // Explicitly allow Exit message to bypass the MAX_QUEUE_SIZE limit.
    // We do not wait on m_cvNotFull here to prevent deadlock during shutdown.
    m_highQueue.push_back(threadMsg);

    // Wake up consumers
    pthread_cond_signal(&m_cvNotEmpty);
    // Wake up blocked producers (DispatchDelegate)
    pthread_cond_broadcast(&m_cvNotFull);

    pthread_mutex_unlock(&m_mutex);

    // Prevent deadlock if ExitThread is called from within the thread itself
    if (!pthread_equal(pthread_self(), m_thread))
    {
        pthread_join(m_thread, nullptr);
    }
    else
    {
        // We are killing ourselves. Detach so the OS cleans up the thread naturally.
        pthread_detach(m_thread);
        // Signal Process() (running on this thread) to exit without touching
        // 'this' again — the owning object is about to be freed.
        if (t_self_exit) *t_self_exit = true;
    }

    pthread_mutex_lock(&m_mutex);
    m_threadCreated = false;
    m_highQueue.clear();
    m_normalQueue.clear();
    // Final cleanup notification
    pthread_cond_broadcast(&m_cvNotFull);
    pthread_mutex_unlock(&m_mutex);
}

//----------------------------------------------------------------------------
// DispatchDelegate
//----------------------------------------------------------------------------
bool PosixThread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    if (m_exit.load() || !m_threadCreated) return false;

    pthread_mutex_lock(&m_mutex);

    // [BACK PRESSURE / DROP / FAULT / TIMEOUT LOGIC]
    if (MAX_QUEUE_SIZE > 0 && (m_highQueue.size() + m_normalQueue.size()) >= MAX_QUEUE_SIZE)
    {
        if (FULL_POLICY == FullPolicy::DROP)
        {
            size_t depth = m_highQueue.size() + m_normalQueue.size();
            pthread_mutex_unlock(&m_mutex);
            if (m_droppedHandler)
                m_droppedHandler(depth);
            return false; // silently discard
        }

        if (FULL_POLICY == FullPolicy::FAULT)
        {
            pthread_mutex_unlock(&m_mutex);
            printf("[Thread] CRITICAL: Queue full on thread '%s'! TRIGGERING FAULT.\n", THREAD_NAME.c_str());
            DMQ_ASSERT_TRUE(false);
            return false;
        }

        if (FULL_POLICY == FullPolicy::TIMEOUT)
        {
            struct timespec ts;
            MakeAbsTimeout(m_dispatchTimeout, ts);
            bool hasSpace = false;
            while (true)
            {
                if ((m_highQueue.size() + m_normalQueue.size()) < MAX_QUEUE_SIZE || m_exit.load())
                {
                    hasSpace = true;
                    break;
                }
                int rc = pthread_cond_timedwait(&m_cvNotFull, &m_mutex, &ts);
                if (rc == ETIMEDOUT)
                {
                    hasSpace = false;
                    break;
                }
            }
            if (!hasSpace)
            {
                size_t depth = m_highQueue.size() + m_normalQueue.size();
                pthread_mutex_unlock(&m_mutex);
                printf("[Thread] WARNING: Queue post timed out on '%s' — possible deadlock. Message dropped.\n", THREAD_NAME.c_str());
                if (m_droppedHandler)
                    m_droppedHandler(depth);
                return false;
            }
            // space found (or exit signaled) — fall through to push/abort check below
        }
    }

    // If using XALLOCATOR explicit operator new required. See xallocator.h.
    auto threadMsg = xmake_shared<ThreadMsg>(MSG_DISPATCH_DELEGATE, msg);
#if defined(DMQ_DATABUS_TOOLS)
    threadMsg->SetEnqueueTime(Timer::GetNow());
#endif

    // If we woke up because of exit (or exit happened while waiting), abort
    if (m_exit.load())
    {
        pthread_mutex_unlock(&m_mutex);
        return false;
    }

    if (threadMsg->GetPriority() == dmq::Priority::HIGH)
        m_highQueue.push_back(threadMsg);
    else
        m_normalQueue.push_back(threadMsg);

#if defined(DMQ_DATABUS_TOOLS)
    // Update monitoring stats
    size_t currentDepth = (m_highQueue.size() + m_normalQueue.size());
    if (currentDepth > m_queueDepthMaxWindow) m_queueDepthMaxWindow = currentDepth;
    if (currentDepth > m_queueDepthMaxAll) m_queueDepthMaxAll = currentDepth;
#endif

    pthread_cond_signal(&m_cvNotEmpty);
    pthread_mutex_unlock(&m_mutex);

    return true;
}

//----------------------------------------------------------------------------
// Process
//----------------------------------------------------------------------------
void PosixThread::Process()
{
    // selfExit is set by ExitThread() when the thread destroys its own owner.
    // It lives on this stack frame so it remains valid even after 'this' is freed.
    bool selfExit = false;
    t_self_exit = &selfExit;

    // Signal that the thread has started processing to notify CreateThread
    pthread_mutex_lock(&m_startMutex);
    m_started = true;
    pthread_cond_signal(&m_startCv);
    pthread_mutex_unlock(&m_startMutex);

    while (!selfExit)
    {
        dmq::Duration watchdogTimeout;
        {
            m_lastAliveTime.store(Timer::GetNow());
            watchdogTimeout = m_watchdogTimeout.load();
        }

        std::shared_ptr<ThreadMsg> msg;

        pthread_mutex_lock(&m_mutex);

        auto queueReady = [this]() { return !(m_highQueue.empty() && m_normalQueue.empty()) || m_exit.load(); };

        // Wait for message to be added to the queue.
        // If watchdog active, use a finite timeout so we can periodically update
        // m_lastAliveTime while idle. Otherwise, block forever.
        if (watchdogTimeout.count() > 0)
        {
            struct timespec ts;
            MakeAbsTimeout(watchdogTimeout / 10, ts);
            while (!queueReady())
            {
                int rc = pthread_cond_timedwait(&m_cvNotEmpty, &m_mutex, &ts);
                if (rc == ETIMEDOUT)
                    break; // Timeout reached, break to update m_lastAliveTime
            }
        }
        else
        {
            while (!queueReady())
                pthread_cond_wait(&m_cvNotEmpty, &m_mutex);
        }

        // Always update alive time immediately after waking up
        m_lastAliveTime.store(Timer::GetNow());

        // If queue still empty, either exit (if requested) or loop again (timeout).
        if (m_highQueue.empty() && m_normalQueue.empty())
        {
            bool doExit = m_exit.load();
            pthread_mutex_unlock(&m_mutex);
            if (doExit) { t_self_exit = nullptr; return; }
            continue;
        }

        // Get highest priority message within queue
        if (!m_highQueue.empty()) {
            msg = m_highQueue.front();
            m_highQueue.pop_front();
        } else {
            msg = m_normalQueue.front();
            m_normalQueue.pop_front();
        }

        // Unblock producers now that space is available
        if (MAX_QUEUE_SIZE > 0)
            pthread_cond_signal(&m_cvNotFull);

        pthread_mutex_unlock(&m_mutex);

        switch (msg->GetId())
        {
            case MSG_DISPATCH_DELEGATE:
            {
#if defined(DMQ_DATABUS_TOOLS)
                // Update latency stats before invoking
                dmq::Duration latency = Timer::GetNow() - msg->GetEnqueueTime();
                {
                    pthread_mutex_lock(&m_mutex);
                    m_latencyTotalWindow += latency;
                    m_latencyCountWindow++;
                    if (latency > m_latencyMaxWindow) m_latencyMaxWindow = latency;
                    if (latency > m_latencyMaxAll) m_latencyMaxAll = latency;
                    m_dispatchCountAll++;
                    pthread_mutex_unlock(&m_mutex);
                }
#endif

                auto delegateMsg = msg->GetData();
                DMQ_ASSERT_TRUE(delegateMsg);
                auto invoker = delegateMsg->GetInvoker();
                DMQ_ASSERT_TRUE(invoker);

#if defined(DMQ_DATABUS_TOOLS)
                dmq::TimePoint start = Timer::GetNow();
#endif
#if defined(__cpp_exceptions) && !defined(DMQ_ASSERTS)
                try {
                    bool success = invoker->Invoke(delegateMsg);
                    if (!selfExit) DMQ_ASSERT_TRUE(success);
                }
                catch (const std::bad_alloc& e) {
                    std::cerr << "[Thread:" << THREAD_NAME << "] Unhandled bad_alloc in delegate callback: " << e.what() << std::endl;
                    DMQ_ASSERT();
                }
                catch (const std::invalid_argument& e) {
                    std::cerr << "[Thread:" << THREAD_NAME << "] Unhandled invalid_argument in delegate callback: " << e.what() << std::endl;
                    DMQ_ASSERT();
                }
                catch (const std::runtime_error& e) {
                    std::cerr << "[Thread:" << THREAD_NAME << "] Unhandled runtime_error in delegate callback: " << e.what() << std::endl;
                    DMQ_ASSERT();
                }
                catch (const std::exception& e) {
                    std::cerr << "[Thread:" << THREAD_NAME << "] Unhandled exception in delegate callback: " << e.what() << std::endl;
                    DMQ_ASSERT();
                }
                catch (...) {
                    std::cerr << "[Thread:" << THREAD_NAME << "] Unhandled unknown exception in delegate callback." << std::endl;
                    DMQ_ASSERT();
                }
#else
                bool success = invoker->Invoke(delegateMsg);
                if (!selfExit) DMQ_ASSERT_TRUE(success);
#endif
                if (selfExit) {
                    t_self_exit = nullptr;
                    return;
                }
#if defined(DMQ_DATABUS_TOOLS)
                dmq::Duration invokeTime = Timer::GetNow() - start;
                if (!selfExit) {
                    pthread_mutex_lock(&m_mutex);
                    m_invokeTotalWindow += invokeTime;
                    m_invokeCountWindow++;
                    if (invokeTime > m_invokeMaxWindow) m_invokeMaxWindow = invokeTime;
                    if (invokeTime > m_invokeMaxAll) m_invokeMaxAll = invokeTime;
                    pthread_mutex_unlock(&m_mutex);
                }
#endif
                break;
            }

            case MSG_EXIT_THREAD:
            {
                t_self_exit = nullptr;
                return;
            }

            default:
            {
                DMQ_ASSERT();
                break;
            }
        }
        // msg goes out of scope here — may trigger self-destruction of 'this'.
        // After this point do not access any member; check selfExit in while().
    }
    t_self_exit = nullptr;
}

//----------------------------------------------------------------------------
// WatchdogCheckAll
//----------------------------------------------------------------------------
void PosixThread::WatchdogCheckAll()
{
    const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
    PosixThread* p = GetWatchdogHead();
    while (p != nullptr)
    {
        p->WatchdogCheck();
        p = p->m_watchdogNext;
    }
}

//----------------------------------------------------------------------------
// WatchdogCheck
//----------------------------------------------------------------------------
void PosixThread::WatchdogCheck()
{
    auto now = Timer::GetNow();
    auto lastAlive = m_lastAliveTime.load();
    auto watchdogTimeout = m_watchdogTimeout.load();

    if (watchdogTimeout.count() > 0)
    {
        auto delta = now - lastAlive;
        if (delta > watchdogTimeout)
        {
            WatchdogHandler(THREAD_NAME.c_str());
        }
    }
}

//----------------------------------------------------------------------------
// ThreadCheck
//----------------------------------------------------------------------------
void PosixThread::ThreadCheck()
{
    m_lastAliveTime.store(Timer::GetNow());
}

//----------------------------------------------------------------------------
// GetWatchdogHead
//----------------------------------------------------------------------------
PosixThread*& PosixThread::GetWatchdogHead()
{
    static PosixThread* head = nullptr;
    return head;
}

//----------------------------------------------------------------------------
// GetWatchdogLock
//----------------------------------------------------------------------------
dmq::RecursiveMutex& PosixThread::GetWatchdogLock()
{
    static dmq::RecursiveMutex* lock = new dmq::RecursiveMutex();
    return *lock;
}

#if defined(DMQ_DATABUS_TOOLS)
//----------------------------------------------------------------------------
// SnapshotStats
//----------------------------------------------------------------------------
PosixThread::ThreadStats PosixThread::SnapshotStats()
{
    pthread_mutex_lock(&m_mutex);
    ThreadStats stats;
    stats.cpu_name = CPU_NAME;
    stats.thread_name = THREAD_NAME;
    stats.queue_depth = (m_highQueue.size() + m_normalQueue.size());
    stats.queue_depth_max_window = m_queueDepthMaxWindow;
    stats.queue_depth_max_all = m_queueDepthMaxAll;
    stats.queue_size_limit = MAX_QUEUE_SIZE;

    if (m_latencyCountWindow > 0) {
        stats.latency_avg_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_latencyTotalWindow).count() / (static_cast<float>(m_latencyCountWindow) * 1000.0f);
    } else {
        stats.latency_avg_ms = 0.0f;
    }

    stats.latency_max_window_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_latencyMaxWindow).count() / 1000.0f;
    stats.latency_max_all_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_latencyMaxAll).count() / 1000.0f;

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
    m_latencyTotalWindow = dmq::Duration(0);
    m_latencyCountWindow = 0;
    m_latencyMaxWindow = dmq::Duration(0);

    m_invokeTotalWindow = dmq::Duration(0);
    m_invokeCountWindow = 0;
    m_invokeMaxWindow = dmq::Duration(0);

    pthread_mutex_unlock(&m_mutex);
    return stats;
}
#endif

} // namespace dmq::os
