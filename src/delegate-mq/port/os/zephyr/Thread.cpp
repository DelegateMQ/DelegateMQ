#ifndef DMQ_THREAD_ZEPHYR
#error "port/os/zephyr/Thread.cpp requires DMQ_THREAD_ZEPHYR. Remove this file from your build configuration or define DMQ_THREAD_ZEPHYR."
#endif

#include "DelegateMQ.h"
#include "Thread.h"
#include "ThreadMsg.h"
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
Thread::Thread(const std::string& threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout)
    : THREAD_NAME(threadName)
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
}

//----------------------------------------------------------------------------
// Thread Destructor
//----------------------------------------------------------------------------
Thread::~Thread()
{
    ExitThread();
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
        // @TODO trigger recovery or fault handler
    }
}

//----------------------------------------------------------------------------
// ThreadCheck
//----------------------------------------------------------------------------
void Thread::ThreadCheck()
{
    m_lastAliveTime.store(Timer::GetNow());
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

    // Signal that we are about to exit
    k_sem_give(&m_exitSem);
}

} // namespace dmq::os
