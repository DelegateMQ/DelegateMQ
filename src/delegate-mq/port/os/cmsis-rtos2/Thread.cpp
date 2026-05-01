#ifndef DMQ_THREAD_CMSIS_RTOS2
#error "port/os/cmsis-rtos2/Thread.cpp requires DMQ_THREAD_CMSIS_RTOS2. Remove this file from your build configuration or define DMQ_THREAD_CMSIS_RTOS2."
#endif

#include "DelegateMQ.h"
#include "Thread.h"
#include "ThreadMsg.h"
#include <cstdio>
#include <new>

// Define ASSERT_TRUE if not already defined
#ifndef ASSERT_TRUE
#define ASSERT_TRUE(x) if(!(x)) { while(1); }
#endif

namespace dmq::os {

//----------------------------------------------------------------------------
// Thread Constructor
//----------------------------------------------------------------------------
Thread::Thread(const std::string& threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout)
    : THREAD_NAME(threadName)
    , m_queueSize((maxQueueSize == 0) ? DEFAULT_QUEUE_SIZE : maxQueueSize)
    , FULL_POLICY(fullPolicy)
    , m_dispatchTimeout(dispatchTimeout)
    , m_exit(false)
{
    // Default Priority
    m_priority = osPriorityNormal;
}

//----------------------------------------------------------------------------
// Thread Destructor
//----------------------------------------------------------------------------
Thread::~Thread()
{
    ExitThread();
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

        m_lastAliveTime.store(dmq::util::Timer::GetNow());

        if (watchdogTimeout.has_value())
        {
            m_watchdogTimeout = watchdogTimeout.value();

            m_threadTimer = std::unique_ptr<dmq::util::Timer>(new dmq::util::Timer());
            m_threadTimerConn = m_threadTimer->OnExpired.Connect(
                dmq::MakeDelegate(this, &Thread::ThreadCheck, *this));
            m_threadTimer->Start(m_watchdogTimeout.load() / 4);

            m_watchdogTimer = std::unique_ptr<dmq::util::Timer>(new dmq::util::Timer());
            m_watchdogTimerConn = m_watchdogTimer->OnExpired.Connect(
                dmq::MakeDelegate(this, &Thread::WatchdogCheck));
            m_watchdogTimer->Start(m_watchdogTimeout.load() / 2);
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
        if (m_threadTimer)
        {
            m_threadTimer->Stop();
            m_threadTimerConn.Disconnect();
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

    // 2. Send pointer to queue
    uint32_t timeout;
    if (FULL_POLICY == FullPolicy::TIMEOUT)
        timeout = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(m_dispatchTimeout).count());
    else
        timeout = 0;  // DROP and FAULT: non-blocking

    // Option #2: Implement High priority using msg_prio.
    uint8_t msg_prio = (msg->GetPriority() == dmq::Priority::HIGH) ? 1 : 0;

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
// Run (Member Function Loop)
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
// WatchdogCheck
//----------------------------------------------------------------------------
void Thread::WatchdogCheck()
{
    auto now = dmq::util::Timer::GetNow();
    auto lastAlive = m_lastAliveTime.load();
    auto delta = now - lastAlive;
    if (delta > m_watchdogTimeout.load())
    {
        // @TODO trigger recovery or fault handler
    }
}

//----------------------------------------------------------------------------
// ThreadCheck
//----------------------------------------------------------------------------
void Thread::ThreadCheck()
{
    m_lastAliveTime.store(dmq::util::Timer::GetNow());
}

void Thread::Run()
{
    ThreadMsg* msg = nullptr;

    while (!m_exit.load())
    {
        dmq::Duration watchdogTimeout;
        {
            m_lastAliveTime.store(dmq::util::Timer::GetNow());
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
                auto delegateMsg = msg->GetData();
                if (delegateMsg) {
                    auto invoker = delegateMsg->GetInvoker();
                    if (invoker) {
                        invoker->Invoke(delegateMsg);
                    }
                }
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

} // namespace dmq::os