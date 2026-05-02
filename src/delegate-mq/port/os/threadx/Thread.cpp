#ifndef DMQ_THREAD_THREADX
#error "port/os/threadx/Thread.cpp requires DMQ_THREAD_THREADX. Remove this file from your build configuration or define DMQ_THREAD_THREADX."
#endif

#include "DelegateMQ.h"
#include "Thread.h"
#include "ThreadMsg.h"
#include <cstdio>
#include <cstring> // for memset
#include <new> // for std::nothrow

// Define ASSERT_TRUE if not already defined
#ifndef ASSERT_TRUE
#define ASSERT_TRUE(x) if(!(x)) { while(1); } // Replace with your fault handler
#endif

namespace dmq::os {

using namespace dmq;
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
    // Zero out control blocks for safety
    memset(&m_thread, 0, sizeof(m_thread));
    memset(&m_queue, 0, sizeof(m_queue));
    memset(&m_exitSem, 0, sizeof(m_exitSem));

    // Default Priority
    m_priority = 10;
}

//----------------------------------------------------------------------------
// Thread Destructor
//----------------------------------------------------------------------------
Thread::~Thread()
{
    ExitThread();

    // Guard against deleting invalid semaphore
    if (m_exitSem.tx_semaphore_id != 0) {
        tx_semaphore_delete(&m_exitSem);
    }
}

//----------------------------------------------------------------------------
// CreateThread
//----------------------------------------------------------------------------
bool Thread::CreateThread(std::optional<dmq::Duration> watchdogTimeout)
{
    // Check if thread is already created (tx_thread_id is non-zero if created)
    if (m_thread.tx_thread_id == 0)
    {
        // 0. Create Synchronization Semaphore (Critical for cleanup)
        if (m_exitSem.tx_semaphore_id == 0) {
            tx_semaphore_create(&m_exitSem, (CHAR*)"ExitSem", 0);
        }

        // --- 1. Create Queue ---
        // ThreadX queues store "words" (ULONGs).
        // We are passing a pointer (ThreadMsg*), so we need enough words to hold a pointer.

        // Round Up Logic (Ceiling division)
        // Ensures we allocate enough words even if pointer size isn't a perfect multiple of ULONG
        UINT msgSizeWords = (sizeof(ThreadMsg*) + sizeof(ULONG) - 1) / sizeof(ULONG);

        // Calculate total ULONGs needed for the queue buffer
        ULONG queueMemSizeWords = m_queueSize * msgSizeWords;
        m_queueMemory.reset(new (std::nothrow) ULONG[queueMemSizeWords]);
        ASSERT_TRUE(m_queueMemory != nullptr);

        UINT ret = tx_queue_create(&m_queue,
                                   (CHAR*)THREAD_NAME.c_str(),
                                   msgSizeWords,
                                   m_queueMemory.get(),
                                   queueMemSizeWords * sizeof(ULONG));
        ASSERT_TRUE(ret == TX_SUCCESS);

        // --- 2. Create Thread ---
        // Stack must be ULONG aligned.

        // Stack Size Rounding
        ULONG stackSizeWords = (STACK_SIZE + sizeof(ULONG) - 1) / sizeof(ULONG);

        m_stackMemory.reset(new (std::nothrow) ULONG[stackSizeWords]);
        ASSERT_TRUE(m_stackMemory != nullptr);

        ret = tx_thread_create(&m_thread,
                               (CHAR*)THREAD_NAME.c_str(),
                               &Thread::Process,
                               (ULONG)(ULONG_PTR)this, // Pass 'this' as entry input (truncated on 64-bit)
                               m_stackMemory.get(),
                               stackSizeWords * sizeof(ULONG),
                               m_priority,
                               m_priority,
                               TX_NO_TIME_SLICE,
                               TX_DONT_START);

        // Store 'this' pointer in user data to avoid truncation issues on 64-bit.
        // We set this BEFORE resuming the thread to avoid a race condition in Process().
        m_thread.tx_thread_user_data = (ULONG_PTR)this;

        if (ret == TX_SUCCESS) {
            tx_thread_resume(&m_thread);
        }

        ASSERT_TRUE(ret == TX_SUCCESS);

        m_lastAliveTime.store(Timer::GetNow());

        if (watchdogTimeout.has_value())
        {
            m_watchdogTimeout = watchdogTimeout.value();

            m_watchdogTimer = std::unique_ptr<Timer>(new Timer());
            m_watchdogTimerConn = m_watchdogTimer->OnExpired.Connect(
                MakeDelegate(this, &Thread::WatchdogCheck));
            m_watchdogTimer->Start(m_watchdogTimeout.load() / 2);
        }
    }
    return true;
}

//----------------------------------------------------------------------------
// SetThreadPriority
//----------------------------------------------------------------------------
void Thread::SetThreadPriority(UINT priority)
{
    m_priority = priority;

    // If the thread is already running, update it live
    if (m_thread.tx_thread_id != 0) {
        UINT oldPriority;
        tx_thread_priority_change(&m_thread, m_priority, &oldPriority);
    }
}

//----------------------------------------------------------------------------
// GetThreadPriority
//----------------------------------------------------------------------------
UINT Thread::GetThreadPriority()
{
    return m_priority;
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void Thread::ExitThread()
{
    if (m_queue.tx_queue_id != 0)
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
            if (tx_queue_send(&m_queue, &msg, TX_WAIT_FOREVER) != TX_SUCCESS)
            {
                delete msg; // Failed to send, prevent leak
            }
        }

        // Wait for thread to terminate using semaphore logic.
        // We only wait if we are NOT the thread itself (avoid deadlock).
        // If tx_thread_identify() returns NULL (ISR context), we also shouldn't block.
        TX_THREAD* currentThread = tx_thread_identify();
        if (currentThread != &m_thread && currentThread != nullptr) {
            // Wait for Run() to signal completion
            tx_semaphore_get(&m_exitSem, TX_WAIT_FOREVER);
        }

        // Force terminate if still running (safety net)
        // tx_thread_terminate returns TX_SUCCESS if terminated or TX_THREAD_ERROR if already terminated
        tx_thread_terminate(&m_thread);
        tx_thread_delete(&m_thread);

        // Delete queue
        tx_queue_delete(&m_queue);

        // Clear control blocks so CreateThread could potentially be called again
        memset(&m_thread, 0, sizeof(m_thread));
        memset(&m_queue, 0, sizeof(m_queue));
    }
}

//----------------------------------------------------------------------------
// GetThreadId
//----------------------------------------------------------------------------
TX_THREAD* Thread::GetThreadId()
{
    return &m_thread;
}

//----------------------------------------------------------------------------
// GetCurrentThreadId
//----------------------------------------------------------------------------
TX_THREAD* Thread::GetCurrentThreadId()
{
    return tx_thread_identify();
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
    if (m_queue.tx_queue_id != 0) {
        ULONG enqueued;
        ULONG available;
        TX_THREAD* suspension_list;
        ULONG suspension_count;
        TX_QUEUE* next_queue;
        if (tx_queue_info_get(&m_queue, TX_NULL, &enqueued, &available, &suspension_list, &suspension_count, &next_queue) == TX_SUCCESS) {
            return (size_t)enqueued;
        }
    }
    return 0;
}

void Thread::Sleep(dmq::Duration timeout) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    // Assuming 100 ticks per second (10ms per tick) as a common default.
    // Ideally use TX_TIMER_TICKS_PER_SECOND if defined.
    ULONG ticks = (static_cast<ULONG>(ms) * TX_TIMER_TICKS_PER_SECOND) / 1000;
    if (ticks == 0 && ms > 0) ticks = 1;
    tx_thread_sleep(ticks);
}

//----------------------------------------------------------------------------
// DispatchDelegate
//----------------------------------------------------------------------------
bool Thread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    // Safety check if queue is valid
    if (m_queue.tx_queue_id == 0) return false;

    // Allocate message container
    ThreadMsg* threadMsg = new (std::nothrow) ThreadMsg(MSG_DISPATCH_DELEGATE, msg);
    if (!threadMsg)
    {
        // OOM: drop the message
        return false;
    }

    // Compute wait option based on policy.
    ULONG wait_option;
    if (FULL_POLICY == FullPolicy::TIMEOUT)
        wait_option = (static_cast<ULONG>(std::chrono::duration_cast<std::chrono::milliseconds>(m_dispatchTimeout).count()) * TX_TIMER_TICKS_PER_SECOND) / 1000;
    else
        wait_option = TX_NO_WAIT;  // DROP and FAULT: non-blocking

    // Option #2: Implement High priority using tx_queue_front_send.
    UINT ret;
    if (msg->GetPriority() == Priority::HIGH)
        ret = tx_queue_front_send(&m_queue, &threadMsg, wait_option);
    else
        ret = tx_queue_send(&m_queue, &threadMsg, wait_option);

    if (ret != TX_SUCCESS)
    {
        if (FULL_POLICY == FullPolicy::FAULT) {
            printf("[Thread] CRITICAL: Queue full on thread '%s'! TRIGGERING FAULT.\n", THREAD_NAME.c_str());
            ASSERT_TRUE(ret == TX_SUCCESS);
        } else if (FULL_POLICY == FullPolicy::TIMEOUT) {
            printf("[Thread] WARNING: Queue post timed out on '%s' — possible deadlock. Message dropped.\n", THREAD_NAME.c_str());
        }
        delete threadMsg; // Failed to enqueue, prevent leak
        return false;
    }
    return true;
}

//----------------------------------------------------------------------------
// Process (Static Entry Point)
//----------------------------------------------------------------------------
void Thread::Process(ULONG instance)
{
    (void)instance;
    // Retrieve the pointer from user data which is ULONG_PTR (pointer width)
    TX_THREAD* current_thread = tx_thread_identify();
    Thread* thread = reinterpret_cast<Thread*>(current_thread->tx_thread_user_data);
    
    ASSERT_TRUE(thread != nullptr);
    thread->Run();
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
        ULONG waitOption = TX_WAIT_FOREVER;
        if (watchdogTimeout.count() > 0)
        {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(watchdogTimeout).count();
            waitOption = (static_cast<ULONG>(ms / 4) * TX_TIMER_TICKS_PER_SECOND) / 1000;
            if (waitOption == 0) waitOption = 1;
        }

        UINT ret = tx_queue_receive(&m_queue, &msg, waitOption);
        if (ret != TX_SUCCESS) continue; // Timeout or other failure
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

    // Signal ExitThread() that the loop has exited
    tx_semaphore_put(&m_exitSem);
}

} // namespace dmq::os
