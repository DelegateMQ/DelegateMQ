#ifndef DMQ_THREAD_CMSIS_RTOS2
#error "port/os/cmsis-rtos2/Thread.cpp requires DMQ_THREAD_CMSIS_RTOS2. Remove this file from your build configuration or define DMQ_THREAD_CMSIS_RTOS2."
#endif

#include "DelegateMQ.h"
#include "Thread.h"
#include "ThreadMsg.h"
#include "extras/util/Fault.h"
#include <cstdio>
#include <new>

// Define ASSERT_TRUE if not already defined
#ifndef ASSERT_TRUE
#define ASSERT_TRUE(x) if(!(x)) { while(1); }
#endif

namespace dmq::os {

using namespace dmq;
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
    // Default Priority
    m_priority = osPriorityNormal;

#if defined(DMQ_DATABUS_TOOLS)
    m_statMutex = osMutexNew(NULL);
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

#if defined(DMQ_DATABUS_TOOLS)
    if (m_statMutex) {
        osMutexDelete(m_statMutex);
        m_statMutex = NULL;
    }
#endif

    // Cleanup semaphore if it exists
    if (m_exitSem) {
        osSemaphoreDelete(m_exitSem);
        m_exitSem = NULL;
    }
}

//----------------------------------------------------------------------------
// CreateThread
//----------------------------------------------------------------------------
bool Thread::CreateThread(std::optional<dmq::Duration> watchdogTimeout)
{
    if (m_thread == NULL)
    {
        // 1. Create Exit Semaphore (Max 1, Initial 0)
        // We use this to wait for the thread to shut down gracefully.
        m_exitSem = osSemaphoreNew(1, 0, NULL);
        ASSERT_TRUE(m_exitSem != NULL);

        // 2. Create Message Queue
        // We store pointers (ThreadMsg*), so msg_size = sizeof(ThreadMsg*)
        m_msgq = osMessageQueueNew(m_queueSize, sizeof(ThreadMsg*), NULL);
        ASSERT_TRUE(m_msgq != NULL);

        // 3. Create Thread
        osThreadAttr_t attr = {0};
        attr.name = THREAD_NAME.c_str();
        attr.stack_size = STACK_SIZE;
        attr.priority = m_priority;

        m_thread = osThreadNew(Thread::Process, this, &attr);
        ASSERT_TRUE(m_thread != NULL);

        m_lastAliveTime.store(Timer::GetNow());

        if (watchdogTimeout.has_value())
        {
            m_watchdogTimeout = watchdogTimeout.value();

            m_watchdogTimer = std::unique_ptr<Timer>(new Timer());
            m_watchdogTimerConn = m_watchdogTimer->OnExpired.Connect(
                MakeDelegate(this, &Thread::WatchdogCheck));
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
// SetThreadPriority
//----------------------------------------------------------------------------
void Thread::SetThreadPriority(osPriority_t priority)
{
    m_priority = priority;

    // If the thread is already running, update it live
    if (m_thread != NULL) {
        osThreadSetPriority(m_thread, m_priority);
    }
}

//----------------------------------------------------------------------------
// GetThreadPriority
//----------------------------------------------------------------------------
osPriority_t Thread::GetThreadPriority()
{
    return m_priority;
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void Thread::ExitThread()
{
    if (m_msgq != NULL)
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
            // Send pointer, wait forever to ensure it gets in.
            if (osMessageQueuePut(m_msgq, &msg, 0, osWaitForever) != osOK) 
            {
                delete msg; // Failed to send
            }
        }

        // Wait for thread to process the exit message and signal completion.
        // We only wait if we are NOT the thread itself (prevent deadlock).
        // If osThreadGetId() returns NULL or error, we skip wait.
        if (osThreadGetId() != m_thread && m_exitSem != NULL) {
            osSemaphoreAcquire(m_exitSem, osWaitForever);
        }

        // Thread has finished Run(). Now we can safely clean up resources.
        m_thread = NULL;

        if (m_msgq) {
             osMessageQueueDelete(m_msgq);
             m_msgq = NULL;
        }
    }
}

//----------------------------------------------------------------------------
// GetThreadId
//----------------------------------------------------------------------------
osThreadId_t Thread::GetThreadId()
{
    return m_thread;
}

//----------------------------------------------------------------------------
// GetCurrentThreadId
//----------------------------------------------------------------------------
osThreadId_t Thread::GetCurrentThreadId()
{
    return osThreadGetId();
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
    if (m_msgq != NULL) {
        return (size_t)osMessageQueueGetCount(m_msgq);
    }
    return 0;
}

void Thread::Sleep(dmq::Duration timeout) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    osDelay(static_cast<uint32_t>(ms));
}

//----------------------------------------------------------------------------
// DispatchDelegate
//----------------------------------------------------------------------------
bool Thread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    ASSERT_TRUE(m_msgq != NULL);

    // 1. Allocate message container
    ThreadMsg* threadMsg = new (std::nothrow) ThreadMsg(MSG_DISPATCH_DELEGATE, msg);
    if (!threadMsg) return false;
    threadMsg->SetEnqueueTime(Timer::GetNow());

    // 2. Send pointer to queue
    uint32_t timeout;
    if (FULL_POLICY == FullPolicy::TIMEOUT)
        timeout = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(m_dispatchTimeout).count());
    else
        timeout = 0;  // DROP and FAULT: non-blocking

    // Option #2: Implement High priority using msg_prio.
    uint8_t msg_prio = (msg->GetPriority() == Priority::HIGH) ? 1 : 0;

    osStatus_t ret = osMessageQueuePut(m_msgq, &threadMsg, msg_prio, timeout);
    if (ret != osOK)
    {
        if (FULL_POLICY == FullPolicy::FAULT) {
            printf("[Thread] CRITICAL: Queue full on thread '%s'! TRIGGERING FAULT.\n", THREAD_NAME.c_str());
            ASSERT_TRUE(ret == osOK);
        } else if (FULL_POLICY == FullPolicy::TIMEOUT) {
            printf("[Thread] WARNING: Queue post timed out on '%s' — possible deadlock. Message dropped.\n", THREAD_NAME.c_str());
        }
        // Failed to send (queue full or timed out)
        delete threadMsg;
        return false;
    }

#if defined(DMQ_DATABUS_TOOLS)
    // Update monitoring stats
    osMutexAcquire(m_statMutex, osWaitForever);
    size_t currentDepth = GetQueueSize();
    if (currentDepth > m_queueDepthMaxWindow) m_queueDepthMaxWindow = currentDepth;
    if (currentDepth > m_queueDepthMaxAll) m_queueDepthMaxAll = currentDepth;
    osMutexRelease(m_statMutex);
#endif

    return true;
}

//----------------------------------------------------------------------------
// Process (Static Entry Point)
//----------------------------------------------------------------------------
void Thread::Process(void* argument)
{
    Thread* thread = static_cast<Thread*>(argument);
    if (thread)
    {
        thread->Run();
    }

    // Thread terminates automatically when function returns.
    osThreadExit();
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
    // Note: No lock acquired here for maximum reliability. 
    // Traversing the list while a thread is added/removed is a race, 
    // but in a steady state (system running) this is safe.
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
        uint32_t waitOption = osWaitForever;
        if (watchdogTimeout.count() > 0)
        {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(watchdogTimeout).count();
            waitOption = static_cast<uint32_t>(ms / 4);
            if (waitOption == 0) waitOption = 1;
        }

        // Block for a message or timeout
        // msg is a pointer to ThreadMsg*. The queue holds the pointer.
        if (osMessageQueueGet(m_msgq, &msg, NULL, waitOption) == osOK)
        {
            if (!msg) continue;

            int msgId = msg->GetId();
            if (msgId == MSG_DISPATCH_DELEGATE)
            {
            #if defined(DMQ_DATABUS_TOOLS)
                // Update latency stats before invoking
                dmq::Duration latency = Timer::GetNow() - msg->GetEnqueueTime();
                {
                    osMutexAcquire(m_statMutex, osWaitForever);
                    m_latencyTotalWindow += latency;
                    m_latencyCountWindow++;
                    if (latency > m_latencyMaxWindow) m_latencyMaxWindow = latency;
                    if (latency > m_latencyMaxAll) m_latencyMaxAll = latency;
                    m_dispatchCountAll++;
                    osMutexRelease(m_statMutex);
                }
            #endif

                auto delegateMsg = msg->GetData();
                ASSERT_TRUE(delegateMsg);
                auto invoker = delegateMsg->GetInvoker();
                ASSERT_TRUE(invoker);
                bool success = invoker->Invoke(delegateMsg);
                ASSERT_TRUE(success);
            }

            delete msg;

            if (msgId == MSG_EXIT_THREAD) {
                break;
            }
        }
    }

    // Signal ExitThread() that we are done
    if (m_exitSem) {
        osSemaphoreRelease(m_exitSem);
    }
}

#if defined(DMQ_DATABUS_TOOLS)
//----------------------------------------------------------------------------
// SnapshotStats
//----------------------------------------------------------------------------
Thread::ThreadStats Thread::SnapshotStats()
{
    osMutexAcquire(m_statMutex, osWaitForever);
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
    stats.dispatch_count = m_dispatchCountAll;

    // Reset windowed stats
    m_queueDepthMaxWindow = stats.queue_depth;
    m_latencyTotalWindow = Duration(0);
    m_latencyCountWindow = 0;
    m_latencyMaxWindow = Duration(0);

    osMutexRelease(m_statMutex);
    return stats;
}
#endif

} // namespace dmq::os
