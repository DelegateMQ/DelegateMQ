#ifndef DMQ_THREAD_ZEPHYR
#error "port/os/zephyr/Thread.cpp requires DMQ_THREAD_ZEPHYR. Remove this file from your build configuration or define DMQ_THREAD_ZEPHYR."
#endif

#include "DelegateMQ.h"
#include "Thread.h"
#include "ThreadMsg.h"
#include "extras/util/Fault.h"
#include <cstdio>
#include <cstring> // for memset

// Define ASSERT_TRUE if not already defined
#ifndef ASSERT_TRUE
#define ASSERT_TRUE(x) __ASSERT(x, "DelegateMQ Assertion Failed")
#endif

namespace dmq::os {

using namespace dmq::util;

//----------------------------------------------------------------------------
// Thread Constructor
//----------------------------------------------------------------------------
Thread::Thread(const std::string& threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout, const std::string& cpuName)
    : THREAD_NAME(threadName)
    , CPU_NAME(cpuName)
    , m_queueSize((maxQueueSize == 0) ? DEFAULT_QUEUE_SIZE : maxQueueSize)
    , FULL_POLICY(fullPolicy)
    , m_dispatchTimeout(dispatchTimeout)
    , m_exit(false)
{
    m_priority = K_PRIO_PREEMPT(5); // Default priority

    // Initialize objects to zero
    memset(&m_thread, 0, sizeof(m_thread));
    memset(&m_msgq, 0, sizeof(m_msgq));
    
    // Initialize exit semaphore (Initial count 0, Limit 1)
    k_sem_init(&m_exitSem, 0, 1);

#if defined(DMQ_DATABUS_TOOLS)
    k_mutex_init(&m_statMutex);
#endif
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
    // Check if thread is already created (dummy check on stack ptr)
    if (!m_stackMemory)
    {
        // 1. Create Message Queue
        // We use k_aligned_alloc to ensure buffer meets strict alignment requirements
        size_t qBufferSize = MSG_SIZE * m_queueSize;
        char* qBuf = (char*)k_aligned_alloc(sizeof(void*), qBufferSize);
        ASSERT_TRUE(qBuf != nullptr);
        
        m_msgqBuffer.reset(qBuf); // Ownership passed to unique_ptr

        k_msgq_init(&m_msgq, m_msgqBuffer.get(), MSG_SIZE, m_queueSize);

        // 2. Create Thread
        // CRITICAL: Stacks must be aligned to Z_KERNEL_STACK_OBJ_ALIGN for MPU/Arch reasons.
        // We use k_aligned_alloc instead of new char[].
        // K_THREAD_STACK_LEN calculates the correct size including guard pages/metadata.
        size_t stackBytes = K_THREAD_STACK_LEN(STACK_SIZE);
        char* stackBuf = (char*)k_aligned_alloc(Z_KERNEL_STACK_OBJ_ALIGN, stackBytes);
        ASSERT_TRUE(stackBuf != nullptr);

        m_stackMemory.reset(stackBuf); // Ownership passed to unique_ptr

        k_tid_t tid = k_thread_create(&m_thread,
                                      (k_thread_stack_t*)m_stackMemory.get(),
                                      STACK_SIZE,
                                      (k_thread_entry_t)Thread::Process,
                                      this, NULL, NULL,
                                      m_priority,
                                      0, 
                                      K_NO_WAIT);
        
        ASSERT_TRUE(tid != nullptr);
        
        // Optional: Set thread name for debug
        k_thread_name_set(tid, THREAD_NAME.c_str());

        m_lastAliveTime.store(Timer::GetNow());

        if (watchdogTimeout.has_value())
        {
            m_watchdogTimeout = watchdogTimeout.value();

            m_watchdogTimer = std::unique_ptr<Timer>(new Timer());
            m_watchdogTimerConn = m_watchdogTimer->OnExpired.Connect(
                dmq::MakeDelegate(this, &Thread::WatchdogCheck));
            m_watchdogTimer->Start(m_watchdogTimeout.load() / 2);

            const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());

            // Add to watchdog registry if not already present
            bool found = false;
            Thread* p = GetWatchdogHead();
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
void Thread::ExitThread()
{
    if (m_stackMemory)
    {
        if (m_watchdogTimer)
        {
            m_watchdogTimer->Stop();
            m_watchdogTimerConn.Disconnect();
        }
        m_exit.store(true);

        // Send exit message
        ThreadMsg* msg = new (std::nothrow) ThreadMsg(MSG_EXIT_THREAD);
        if (msg)
        {
            // Wait forever to ensure message is sent
            if (k_msgq_put(&m_msgq, &msg, K_FOREVER) != 0) 
            {
                delete msg; 
            }
        }
        
        // Wait for thread to actually finish to avoid use-after-free of the stack.
        // We only wait if we are NOT the thread itself (avoid deadlock).
        if (k_current_get() != &m_thread) {
            k_sem_take(&m_exitSem, K_FOREVER);
        }

        // Reset buffers to mark as exited. This prevents a double-entry deadlock:
        // ~Thread() calls ExitThread() unconditionally, so if ExitThread() was already
        // called explicitly, the second call must be a no-op (m_stackMemory is null).
        m_stackMemory.reset();
        m_msgqBuffer.reset();

        // Note: k_thread_abort is not needed because the thread will
        // return from Run() and terminate naturally.
    }
}

//----------------------------------------------------------------------------
// SetThreadPriority
//----------------------------------------------------------------------------
void Thread::SetThreadPriority(int priority)
{
    m_priority = priority;
    if (m_stackMemory) {
        k_thread_priority_set(&m_thread, m_priority);
    }
}

//----------------------------------------------------------------------------
// GetThreadId
//----------------------------------------------------------------------------
k_tid_t Thread::GetThreadId()
{
    return &m_thread;
}

//----------------------------------------------------------------------------
// GetCurrentThreadId
//----------------------------------------------------------------------------
k_tid_t Thread::GetCurrentThreadId()
{
    return k_current_get();
}

//----------------------------------------------------------------------------
// IsCurrentThread
//----------------------------------------------------------------------------
bool Thread::IsCurrentThread()
{
    return GetThreadId() == GetCurrentThreadId();
}

//----------------------------------------------------------------------------
// GetQueueSize
//----------------------------------------------------------------------------
size_t Thread::GetQueueSize()
{
    if (m_stackMemory) {
        return (size_t)k_msgq_num_used_get(&m_msgq);
    }
    return 0;
}

void Thread::Sleep(dmq::Duration timeout) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    k_sleep(K_MSEC(ms));
}

//----------------------------------------------------------------------------
// DispatchDelegate
//----------------------------------------------------------------------------
bool Thread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    ASSERT_TRUE(m_stackMemory != nullptr);

    // 1. Allocate message container
    ThreadMsg* threadMsg = new (std::nothrow) ThreadMsg(MSG_DISPATCH_DELEGATE, msg);
    if (!threadMsg) return false;
#if defined(DMQ_DATABUS_TOOLS)
    threadMsg->SetEnqueueTime(Timer::GetNow());
#endif

    // 2. Send pointer to queue
    k_timeout_t timeout;
    if (FULL_POLICY == FullPolicy::TIMEOUT)
        timeout = K_MSEC(std::chrono::duration_cast<std::chrono::milliseconds>(m_dispatchTimeout).count());
    else
        timeout = K_NO_WAIT;  // DROP and FAULT: non-blocking

    int ret = k_msgq_put(&m_msgq, &threadMsg, timeout);
    if (ret != 0)
    {
        if (FULL_POLICY == FullPolicy::FAULT) {
            printf("[Thread] CRITICAL: Queue full on thread '%s'! TRIGGERING FAULT.\n", THREAD_NAME.c_str());
            ASSERT_TRUE(ret == 0);
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
        k_mutex_lock(&m_statMutex, K_FOREVER);
        size_t currentDepth = GetQueueSize();
        if (currentDepth > m_queueDepthMaxWindow) m_queueDepthMaxWindow = currentDepth;
        if (currentDepth > m_queueDepthMaxAll) m_queueDepthMaxAll = currentDepth;
        k_mutex_unlock(&m_statMutex);
    }
#endif

    return true;
}

//----------------------------------------------------------------------------
// Process (Static Entry Point)
//----------------------------------------------------------------------------
void Thread::Process(void* p1, void* p2, void* p3)
{
    Thread* thread = static_cast<Thread*>(p1);
    if (thread)
    {
        thread->Run();
    }
    // Returning from entry point automatically terminates the thread in Zephyr
}

//----------------------------------------------------------------------------
// Run (Member Function Loop)
//----------------------------------------------------------------------------
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
        WatchdogHandler(THREAD_NAME.c_str());
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
// Run (Member Function Loop)
//----------------------------------------------------------------------------
void Thread::Run()
{
    ThreadMsg* msg = nullptr;
    while (!m_exit.load())
    {
        dmq::Duration watchdogTimeout;
        {
            m_lastAliveTime.store(Timer::GetNow());
            watchdogTimeout = m_watchdogTimeout.load();
        }

        // If watchdog active, use a finite timeout so we can periodically update 
        // m_lastAliveTime while idle. Otherwise, block forever to save power.
        k_timeout_t waitOption = K_FOREVER;
        if (watchdogTimeout.count() > 0)
        {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(watchdogTimeout).count();
            waitOption = K_MSEC(ms / 4);
            if (K_TIMEOUT_EQ(waitOption, K_NO_WAIT)) waitOption = K_MSEC(1);
        }

        // Block for a message or timeout
        if (k_msgq_get(&m_msgq, &msg, waitOption) == 0)
        {
            if (!msg) continue;

            int msgId = msg->GetId();
            if (msgId == MSG_DISPATCH_DELEGATE)
            {
#if defined(DMQ_DATABUS_TOOLS)
                // Update latency stats before invoking
                dmq::Duration latency = Timer::GetNow() - msg->GetEnqueueTime();
                {
                    k_mutex_lock(&m_statMutex, K_FOREVER);
                    m_latencyTotalWindow += latency;
                    m_latencyCountWindow++;
                    if (latency > m_latencyMaxWindow) m_latencyMaxWindow = latency;
                    if (latency > m_latencyMaxAll) m_latencyMaxAll = latency;
                    m_dispatchCountAll++;
                    k_mutex_unlock(&m_statMutex);
                }
#endif

                auto delegateMsg = msg->GetData();
                ASSERT_TRUE(delegateMsg);
                auto invoker = delegateMsg->GetInvoker();
                ASSERT_TRUE(invoker);

#if defined(DMQ_DATABUS_TOOLS)
                dmq::TimePoint start = Timer::GetNow();
#endif
                bool success = invoker->Invoke(delegateMsg);
#if defined(DMQ_DATABUS_TOOLS)
                dmq::Duration invokeTime = Timer::GetNow() - start;
                {
                    k_mutex_lock(&m_statMutex, K_FOREVER);
                    m_invokeTotalWindow += invokeTime;
                    m_invokeCountWindow++;
                    if (invokeTime > m_invokeMaxWindow) m_invokeMaxWindow = invokeTime;
                    if (invokeTime > m_invokeMaxAll) m_invokeMaxAll = invokeTime;
                    k_mutex_unlock(&m_statMutex);
                }
#endif
                ASSERT_TRUE(success);
            }
            
            delete msg;

            if (msgId == MSG_EXIT_THREAD) {
                break;
            }
        }
    }

    // Signal that we are about to exit
    k_sem_give(&m_exitSem);
}

#if defined(DMQ_DATABUS_TOOLS)
//----------------------------------------------------------------------------
// SnapshotStats
//----------------------------------------------------------------------------
Thread::ThreadStats Thread::SnapshotStats()
{
    k_mutex_lock(&m_statMutex, K_FOREVER);
    ThreadStats stats;
    stats.cpu_name = CPU_NAME;
    stats.thread_name = THREAD_NAME;
    stats.queue_depth = GetQueueSize();
    stats.queue_depth_max_window = m_queueDepthMaxWindow;
    stats.queue_depth_max_all = m_queueDepthMaxAll;
    stats.queue_size_limit = m_queueSize;
    
    if (m_latencyCountWindow > 0) {
        stats.latency_avg_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_latencyTotalWindow).count() / (m_latencyCountWindow * 1000.0f);
    } else {
        stats.latency_avg_ms = 0.0f;
    }

    stats.latency_max_window_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_latencyMaxWindow).count() / 1000.0f;
    stats.latency_max_all_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(m_latencyMaxAll).count() / 1000.0f;

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

    k_mutex_unlock(&m_statMutex);
    return stats;
}
#endif

} // namespace dmq::os
