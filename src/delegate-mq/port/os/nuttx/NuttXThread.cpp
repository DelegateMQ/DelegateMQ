#ifndef DMQ_THREAD_NUTTX
#error "port/os/nuttx/NuttXThread.cpp requires DMQ_THREAD_NUTTX. Remove this file from your build configuration or define DMQ_THREAD_NUTTX."
#endif

#include "DelegateMQ.h"
#include "NuttXThread.h"
#include "port/os/common/ThreadMsg.h"
#include "extras/util/Fault.h"
#include <cstdio>
#include <cstring> // for memset
#include <cassert>

// Define ASSERT_TRUE if not already defined
#ifndef ASSERT_TRUE
#define ASSERT_TRUE(x) assert(x)
#endif

namespace dmq::os {

using namespace dmq::util;

//----------------------------------------------------------------------------
// Thread Constructor
//----------------------------------------------------------------------------
NuttXThread::NuttXThread(const char* threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout, const char* cpuName)
    : THREAD_NAME(threadName)
    , CPU_NAME(cpuName)
    , m_queueSize((maxQueueSize == 0) ? DEFAULT_QUEUE_SIZE : maxQueueSize)
    , FULL_POLICY(fullPolicy)
    , m_dispatchTimeout(dispatchTimeout)
    , m_exit(false)
{
    m_priority = 100; // Default SCHED_FIFO priority (NuttX default range is typically 1-255)

    sem_init(&m_exitSem, 0, 0);

#if defined(DMQ_DATABUS_TOOLS)
    pthread_mutex_init(&m_statMutex, nullptr);
#endif
}

//----------------------------------------------------------------------------
// Thread Destructor
//----------------------------------------------------------------------------
NuttXThread::~NuttXThread()
{
    ExitThread();

    sem_destroy(&m_exitSem);

#if defined(DMQ_DATABUS_TOOLS)
    pthread_mutex_destroy(&m_statMutex);
#endif

    const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
    NuttXThread** pp = &GetWatchdogHead();
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
bool NuttXThread::CreateThread(std::optional<dmq::Duration> watchdogTimeout)
{
    if (!m_created.load())
    {
        // 1. Create the message queue
        ASSERT_TRUE(m_queue.Create(m_queueSize));

        // 2. Create the pthread
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, STACK_SIZE);

        struct sched_param sp;
        sp.sched_priority = m_priority;
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        pthread_attr_setschedparam(&attr, &sp);

        int rc = pthread_create(&m_thread, &attr, NuttXThread::Process, this);
        pthread_attr_destroy(&attr);
        ASSERT_TRUE(rc == 0);

#if defined(PTHREAD_NAME_MAX) || defined(CONFIG_TASK_NAME_SIZE)
        // NuttX supports naming a pthread for debugging (non-portable POSIX
        // extension); harmless no-op if unsupported on a given configuration.
        pthread_setname_np(m_thread, THREAD_NAME.c_str());
#endif

        m_created.store(true);
        m_lastAliveTime.store(Timer::GetNow());

        if (watchdogTimeout.has_value())
        {
            m_watchdogTimeout = watchdogTimeout.value();

            const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());

            // Add to watchdog registry if not already present
            bool found = false;
            NuttXThread* p = GetWatchdogHead();
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
    return true;
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void NuttXThread::ExitThread()
{
    if (m_created.load())
    {
        m_exit.store(true);

        // Check self-exit BEFORE attempting to enqueue the exit message. If
        // this thread is destroying itself from within its own dispatched
        // callback, it is not consuming its own queue right now -- it is
        // blocked here, inside ExitThread(). A blocking mq_send() would
        // deadlock forever if the queue happened to be full. No message
        // needs to be queued in that case: Run()'s dispatch loop already
        // checks m_selfExitPtr immediately after the current callback
        // invoke returns, and unwinds without touching 'this' again.
        //
        // Crucially, a self-exiting thread must NOT touch m_queue here: it
        // is still physically executing on its own stack (unwinding back
        // through Invoke()/Run()), and it cannot pthread_join() itself. Set
        // the flag and return immediately; the actual cleanup (queue
        // drain/destroy, pthread_join) is deferred to a later ExitThread()
        // call made from a different thread context -- typically
        // ~NuttXThread() -- once m_selfExited tells that call it's safe to
        // skip the message/semaphore handshake below (the thread already
        // returned/is returning on its own) and go straight to the join.
        if (pthread_equal(pthread_self(), m_thread)) {
            m_selfExited.store(true);
            if (m_selfExitPtr) *m_selfExitPtr = true;
            return;
        }

        if (!m_selfExited.load())
        {
            // Send exit message
            ThreadMsg* msg = new (std::nothrow) ThreadMsg(MSG_EXIT_THREAD);
            if (msg)
            {
                // Wait forever to ensure message is sent
                if (!m_queue.Send(msg, /*highPriority=*/false, NuttXDelegateQueue::WAIT_FOREVER))
                {
                    delete msg;
                }
            }

            // Wait for thread to actually finish before joining.
            sem_wait(&m_exitSem);
        }

        // pthread_join() blocks until the kernel has fully torn the thread
        // down, the race-free way to wait for that -- sem_wait() above only
        // proves Run() is about to return, not that the kernel has finished
        // unlinking it from scheduler structures. It is also the right call
        // in the deferred self-exit case: the thread has already returned or
        // is in the process of returning on its own, so this either returns
        // immediately or waits only as long as that natural teardown takes.
        pthread_join(m_thread, nullptr);

        m_queue.DrainAndDelete();
        m_queue.Destroy();

        // Reset flag to mark as exited. This prevents a double-entry deadlock:
        // ~NuttXThread() calls ExitThread() unconditionally, so if ExitThread()
        // was already called explicitly, the second call must be a no-op.
        m_created.store(false);

        // Allow this object to be reused for a fresh CreateThread()/Run() cycle.
        m_selfExited.store(false);
    }
}

//----------------------------------------------------------------------------
// SetThreadPriority
//----------------------------------------------------------------------------
void NuttXThread::SetThreadPriority(int priority)
{
    m_priority = priority;
    if (m_created.load()) {
        struct sched_param sp;
        sp.sched_priority = m_priority;
        pthread_setschedparam(m_thread, SCHED_FIFO, &sp);
    }
}

//----------------------------------------------------------------------------
// GetThreadId
//----------------------------------------------------------------------------
pthread_t NuttXThread::GetThreadId()
{
    return m_thread;
}

//----------------------------------------------------------------------------
// GetCurrentThreadId
//----------------------------------------------------------------------------
pthread_t NuttXThread::GetCurrentThreadId()
{
    return pthread_self();
}

//----------------------------------------------------------------------------
// IsCurrentThread
//----------------------------------------------------------------------------
bool NuttXThread::IsCurrentThread()
{
    if (!m_created.load())
        return false;
    return pthread_equal(GetThreadId(), GetCurrentThreadId()) != 0;
}

//----------------------------------------------------------------------------
// GetQueueSize
//----------------------------------------------------------------------------
size_t NuttXThread::GetQueueSize()
{
    return m_queue.Size();
}

void NuttXThread::Sleep(dmq::Duration timeout) {
    dmq::ThisThread::sleep_for(timeout);
}

//----------------------------------------------------------------------------
// DispatchDelegate
//----------------------------------------------------------------------------
bool NuttXThread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    ASSERT_TRUE(m_created.load());

    // 1. Allocate message container
    ThreadMsg* threadMsg = new (std::nothrow) ThreadMsg(MSG_DISPATCH_DELEGATE, msg);
    if (!threadMsg) return false;
#if defined(DMQ_DATABUS_TOOLS)
    threadMsg->SetEnqueueTime(Timer::GetNow());
#endif

    // 2. Send pointer to queue
    dmq::Duration timeout;
    if (FULL_POLICY == FullPolicy::TIMEOUT)
        timeout = m_dispatchTimeout;
    else
        timeout = dmq::Duration::zero();  // DROP and FAULT: non-blocking

    // High priority routes through mq_send()'s native msg_prio argument.
    bool sent = m_queue.Send(threadMsg, msg->GetPriority() == dmq::Priority::HIGH, timeout);
    if (!sent)
    {
        if (FULL_POLICY == FullPolicy::FAULT) {
            printf("[Thread] CRITICAL: Queue full on thread '%s'! TRIGGERING FAULT.\n", THREAD_NAME.c_str());
            ASSERT_TRUE(sent);
        } else if (FULL_POLICY == FullPolicy::TIMEOUT) {
            printf("[Thread] WARNING: Queue post timed out on '%s' — possible deadlock. Message dropped.\n", THREAD_NAME.c_str());
        }
        // Failed to enqueue (queue full or timed out)
        delete threadMsg;
        return false;
    }

#if defined(DMQ_DATABUS_TOOLS)
    // Update monitoring stats
    {
        pthread_mutex_lock(&m_statMutex);
        size_t currentDepth = GetQueueSize();
        if (currentDepth > m_queueDepthMaxWindow) m_queueDepthMaxWindow = currentDepth;
        if (currentDepth > m_queueDepthMaxAll) m_queueDepthMaxAll = currentDepth;
        pthread_mutex_unlock(&m_statMutex);
    }
#endif

    return true;
}

//----------------------------------------------------------------------------
// Process (Static Entry Point)
//----------------------------------------------------------------------------
void* NuttXThread::Process(void* arg)
{
    NuttXThread* thread = static_cast<NuttXThread*>(arg);
    if (thread)
    {
        thread->Run();
    }
    return nullptr;
}

//----------------------------------------------------------------------------
// WatchdogCheck
//----------------------------------------------------------------------------
void NuttXThread::WatchdogCheck()
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
void NuttXThread::ThreadCheck()
{
    m_lastAliveTime.store(Timer::GetNow());
}

//----------------------------------------------------------------------------
// WatchdogCheckAll
//----------------------------------------------------------------------------
void NuttXThread::WatchdogCheckAll()
{
    const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
    NuttXThread* p = GetWatchdogHead();
    while (p != nullptr)
    {
        p->WatchdogCheck();
        p = p->m_watchdogNext;
    }
}

//----------------------------------------------------------------------------
// GetWatchdogHead
//----------------------------------------------------------------------------
NuttXThread*& NuttXThread::GetWatchdogHead()
{
    static NuttXThread* head = nullptr;
    return head;
}

//----------------------------------------------------------------------------
// GetWatchdogLock
//----------------------------------------------------------------------------
dmq::RecursiveMutex& NuttXThread::GetWatchdogLock()
{
    static dmq::RecursiveMutex* lock = new dmq::RecursiveMutex();
    return *lock;
}

//----------------------------------------------------------------------------
// Run (Member Function Loop)
//----------------------------------------------------------------------------
void NuttXThread::Run()
{
    bool selfExit = false;
    m_selfExitPtr = &selfExit;

    ThreadMsg* msg = nullptr;
    while (!selfExit && !m_exit.load())
    {
        dmq::Duration watchdogTimeout;
        {
            m_lastAliveTime.store(Timer::GetNow());
            watchdogTimeout = m_watchdogTimeout.load();
        }

        // If watchdog active, use a finite timeout so we can periodically update
        // m_lastAliveTime while idle. Otherwise, block forever to save power.
        dmq::Duration waitOption = NuttXDelegateQueue::WAIT_FOREVER;
        if (watchdogTimeout.count() > 0)
        {
            waitOption = watchdogTimeout / 4;
            if (waitOption <= dmq::Duration::zero())
                waitOption = std::chrono::duration_cast<dmq::Duration>(std::chrono::milliseconds(1));
        }

        // Block for a message or timeout
        msg = m_queue.Receive(waitOption);
        if (msg != nullptr)
        {
            int msgId = msg->GetId();
            if (msgId == MSG_DISPATCH_DELEGATE)
            {
#if defined(DMQ_DATABUS_TOOLS)
                // Update latency stats before invoking
                dmq::Duration latency = Timer::GetNow() - msg->GetEnqueueTime();
                {
                    pthread_mutex_lock(&m_statMutex);
                    m_latencyTotalWindow += latency;
                    m_latencyCountWindow++;
                    if (latency > m_latencyMaxWindow) m_latencyMaxWindow = latency;
                    if (latency > m_latencyMaxAll) m_latencyMaxAll = latency;
                    m_dispatchCountAll++;
                    pthread_mutex_unlock(&m_statMutex);
                }
#endif

                auto delegateMsg = msg->GetData();
                ASSERT_TRUE(delegateMsg);
                auto invoker = delegateMsg->GetInvoker();
                ASSERT_TRUE(invoker);

#if defined(DMQ_DATABUS_TOOLS)
                dmq::TimePoint start = Timer::GetNow();
#endif
#if defined(__cpp_exceptions) && !defined(DMQ_ASSERTS)
                bool success = false;
                try {
                    success = invoker->Invoke(delegateMsg);
                    ASSERT_TRUE(success);
                }
                catch (const std::bad_alloc& e) {
                    std::cerr << "[Thread:" << THREAD_NAME << "] Unhandled bad_alloc in delegate callback: " << e.what() << std::endl;
                    ASSERT();
                }
                catch (const std::invalid_argument& e) {
                    std::cerr << "[Thread:" << THREAD_NAME << "] Unhandled invalid_argument in delegate callback: " << e.what() << std::endl;
                    ASSERT();
                }
                catch (const std::runtime_error& e) {
                    std::cerr << "[Thread:" << THREAD_NAME << "] Unhandled runtime_error in delegate callback: " << e.what() << std::endl;
                    ASSERT();
                }
                catch (const std::exception& e) {
                    printf("[Thread:%s] Unhandled exception in delegate callback: %s\n", THREAD_NAME.c_str(), e.what());
                    ASSERT();
                }
                catch (...) {
                    printf("[Thread:%s] Unhandled unknown exception in delegate callback.\n", THREAD_NAME.c_str());
                    ASSERT();
                }
#else
                bool success = invoker->Invoke(delegateMsg);
                if (!selfExit) ASSERT_TRUE(success);
#endif
                if (selfExit) {
                    delete msg;
                    m_selfExitPtr = nullptr;
                    return;
                }

#if defined(DMQ_DATABUS_TOOLS)
                dmq::Duration invokeTime = Timer::GetNow() - start;
                {
                    pthread_mutex_lock(&m_statMutex);
                    m_invokeTotalWindow += invokeTime;
                    m_invokeCountWindow++;
                    if (invokeTime > m_invokeMaxWindow) m_invokeMaxWindow = invokeTime;
                    if (invokeTime > m_invokeMaxAll) m_invokeMaxAll = invokeTime;
                    pthread_mutex_unlock(&m_statMutex);
                }
#endif
            }

            delete msg;

            if (msgId == MSG_EXIT_THREAD) {
                break;
            }
        }
    }

    // Signal that we are about to exit
    sem_post(&m_exitSem);
    m_selfExitPtr = nullptr;
}

#if defined(DMQ_DATABUS_TOOLS)
//----------------------------------------------------------------------------
// SnapshotStats
//----------------------------------------------------------------------------
NuttXThread::ThreadStats NuttXThread::SnapshotStats()
{
    pthread_mutex_lock(&m_statMutex);
    ThreadStats stats;
    stats.cpu_name = CPU_NAME;
    stats.thread_name = THREAD_NAME;
    stats.queue_depth = GetQueueSize();
    stats.queue_depth_max_window = m_queueDepthMaxWindow;
    stats.queue_depth_max_all = m_queueDepthMaxAll;
    stats.queue_size_limit = m_queueSize;

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
    m_latencyTotalWindow = Duration(0);
    m_latencyCountWindow = 0;
    m_latencyMaxWindow = Duration(0);

    m_invokeTotalWindow = Duration(0);
    m_invokeCountWindow = 0;
    m_invokeMaxWindow = Duration(0);

    pthread_mutex_unlock(&m_statMutex);
    return stats;
}
#endif

} // namespace dmq::os
