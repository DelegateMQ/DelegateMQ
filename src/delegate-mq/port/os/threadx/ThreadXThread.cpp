#ifndef DMQ_THREAD_THREADX
#error "port/os/threadx/ThreadXThread.cpp requires DMQ_THREAD_THREADX. Remove this file from your build configuration or define DMQ_THREAD_THREADX."
#endif

#include "DelegateMQ.h"
#include "ThreadXThread.h"
#include "port/os/common/ThreadMsg.h"
#include "extras/util/Fault.h"
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
ThreadXThread::ThreadXThread(const char* threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout, const char* cpuName)
    : THREAD_NAME(threadName)
    , CPU_NAME(cpuName)
    , m_queueSize((maxQueueSize == 0) ? DEFAULT_QUEUE_SIZE : maxQueueSize)
    , FULL_POLICY(fullPolicy)
    , m_dispatchTimeout(dispatchTimeout)
    , m_exit(false)
{
    // Zero out control blocks for safety (m_queue zeroes its own control
    // block in ThreadXDelegateQueue's constructor)
    memset(&m_thread, 0, sizeof(m_thread));
    memset(&m_exitSem, 0, sizeof(m_exitSem));

#if defined(DMQ_DATABUS_TOOLS)
    tx_mutex_create(&m_statMutex, (CHAR*)"StatMutex", TX_NO_INHERIT);
#endif

    // Default Priority
    m_priority = 10;
}

//----------------------------------------------------------------------------
// Thread Destructor
//----------------------------------------------------------------------------
ThreadXThread::~ThreadXThread()
{
    ExitThread();

    const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
    ThreadXThread** pp = &GetWatchdogHead();
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
    tx_mutex_delete(&m_statMutex);
#endif

    // Guard against deleting invalid semaphore
    if (m_exitSem.tx_semaphore_id != 0) {
        tx_semaphore_delete(&m_exitSem);
    }
}

//----------------------------------------------------------------------------
// CreateThread
//----------------------------------------------------------------------------
bool ThreadXThread::CreateThread(std::optional<dmq::Duration> watchdogTimeout)
{
    // Check if thread is already created (tx_thread_id is non-zero if created)
    if (m_thread.tx_thread_id == 0)
    {
        // 0. Create Synchronization Semaphore (Critical for cleanup)
        if (m_exitSem.tx_semaphore_id == 0) {
            tx_semaphore_create(&m_exitSem, (CHAR*)"ExitSem", 0);
        }

        // --- 1. Create Queue ---
        ASSERT_TRUE(m_queue.Create(m_queueSize, THREAD_NAME.c_str()));

        // --- 2. Create Thread ---
        // Stack must be ULONG aligned.

        // Stack Size Rounding
        ULONG stackSizeWords = (STACK_SIZE + sizeof(ULONG) - 1) / sizeof(ULONG);

        m_stackMemory.reset(new (std::nothrow) ULONG[stackSizeWords]);
        ASSERT_TRUE(m_stackMemory != nullptr);

        UINT ret = tx_thread_create(&m_thread,
                               (CHAR*)THREAD_NAME.c_str(),
                               &ThreadXThread::Process,
                               0, // Unused: see GetInstanceRegistry()
                               m_stackMemory.get(),
                               stackSizeWords * sizeof(ULONG),
                               m_priority,
                               m_priority,
                               TX_NO_TIME_SLICE,
                               TX_DONT_START);

        if (ret == TX_SUCCESS) {
            // Register before resuming: the thread must never be able to run
            // (and call Process()) before its registry entry exists.
            {
                const std::lock_guard<dmq::RecursiveMutex> lock(GetInstanceRegistryLock());
                GetInstanceRegistry()[&m_thread] = this;
            }
            tx_thread_resume(&m_thread);
        }

        ASSERT_TRUE(ret == TX_SUCCESS);

        m_lastAliveTime.store(Timer::GetNow());

        if (watchdogTimeout.has_value())
        {
            m_watchdogTimeout = watchdogTimeout.value();

            const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());

            // Add to watchdog registry if not already present
            bool found = false;
            ThreadXThread* p = GetWatchdogHead();
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
void ThreadXThread::SetThreadPriority(UINT priority)
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
UINT ThreadXThread::GetThreadPriority()
{
    return m_priority;
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void ThreadXThread::ExitThread()
{
    if (m_queue.IsCreated())
    {
        m_exit.store(true);

        // Determine self-exit BEFORE attempting to enqueue the exit message.
        // If this thread is destroying itself from within its own dispatched
        // callback, it is not consuming its own queue right now -- it is
        // blocked here, inside ExitThread(). A blocking tx_queue_send() would
        // deadlock forever if the queue happened to be full. No message needs
        // to be queued in that case: Run()'s dispatch loop already checks
        // m_selfExitPtr immediately after the current callback invoke
        // returns, and unwinds without touching 'this' again.
        TX_THREAD* currentThread = tx_thread_identify();
        bool isSelfExit = (currentThread == &m_thread);

        if (isSelfExit) {
            if (m_selfExitPtr) *m_selfExitPtr = true;
        } else {
            // Send exit message
            ThreadMsg* msg = new (std::nothrow) ThreadMsg(MSG_EXIT_THREAD);
            if (msg)
            {
                // Wait forever to ensure message is sent
                if (!m_queue.Send(msg, /*highPriority=*/false, TX_WAIT_FOREVER))
                {
                    delete msg; // Failed to send, prevent leak
                }
            }

            // If tx_thread_identify() returns NULL (ISR context), we shouldn't block.
            if (currentThread != nullptr) {
                // Wait for Run() to signal completion
                tx_semaphore_get(&m_exitSem, TX_WAIT_FOREVER);
            }

            // Force terminate if still running (safety net)
            // tx_thread_terminate returns TX_SUCCESS if terminated or TX_THREAD_ERROR if already terminated
            tx_thread_terminate(&m_thread);
            tx_thread_delete(&m_thread);
        }

        m_queue.DrainAndDelete();
        m_queue.Destroy();

        // Remove the registry entry before clearing/reusing the control block
        // (see GetInstanceRegistry() for why this exists).
        {
            const std::lock_guard<dmq::RecursiveMutex> lock(GetInstanceRegistryLock());
            GetInstanceRegistry().erase(&m_thread);
        }

        // Clear control block so CreateThread could potentially be called again
        memset(&m_thread, 0, sizeof(m_thread));
    }
}

//----------------------------------------------------------------------------
// GetThreadId
//----------------------------------------------------------------------------
TX_THREAD* ThreadXThread::GetThreadId()
{
    return &m_thread;
}

//----------------------------------------------------------------------------
// GetCurrentThreadId
//----------------------------------------------------------------------------
TX_THREAD* ThreadXThread::GetCurrentThreadId()
{
    return tx_thread_identify();
}

//----------------------------------------------------------------------------
// IsCurrentThread
//----------------------------------------------------------------------------
bool ThreadXThread::IsCurrentThread()
{
    return GetThreadId() == GetCurrentThreadId();
}

//----------------------------------------------------------------------------
// GetQueueSize
//----------------------------------------------------------------------------
size_t ThreadXThread::GetQueueSize()
{
    return m_queue.Size();
}

void ThreadXThread::Sleep(dmq::Duration timeout) {
    dmq::ThisThread::sleep_for(timeout);
}

//----------------------------------------------------------------------------
// DispatchDelegate
//----------------------------------------------------------------------------
bool ThreadXThread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    // Safety check if queue is valid
    if (!m_queue.IsCreated()) return false;

    // Allocate message container
    ThreadMsg* threadMsg = new (std::nothrow) ThreadMsg(MSG_DISPATCH_DELEGATE, msg);
    if (!threadMsg)
    {
        // OOM: drop the message
        return false;
    }
#if defined(DMQ_DATABUS_TOOLS)
    threadMsg->SetEnqueueTime(Timer::GetNow());
#endif

    // Compute wait option based on policy.
    ULONG wait_option;
    if (FULL_POLICY == FullPolicy::TIMEOUT)
        wait_option = (static_cast<ULONG>(std::chrono::duration_cast<std::chrono::milliseconds>(m_dispatchTimeout).count()) * TX_TIMER_TICKS_PER_SECOND) / 1000;
    else
        wait_option = TX_NO_WAIT;  // DROP and FAULT: non-blocking

    // High priority jumps the FIFO via tx_queue_front_send; all others go to the back.
    bool sent = m_queue.Send(threadMsg, msg->GetPriority() == Priority::HIGH, wait_option);

    if (!sent)
    {
        if (FULL_POLICY == FullPolicy::FAULT) {
            printf("[Thread] CRITICAL: Queue full on thread '%s'! TRIGGERING FAULT.\n", THREAD_NAME.c_str());
            ASSERT_TRUE(sent);
        } else if (FULL_POLICY == FullPolicy::TIMEOUT) {
            printf("[Thread] WARNING: Queue post timed out on '%s' — possible deadlock. Message dropped.\n", THREAD_NAME.c_str());
        }
        delete threadMsg; // Failed to enqueue, prevent leak
        return false;
    }

#if defined(DMQ_DATABUS_TOOLS)
    // Update monitoring stats
    tx_mutex_get(&m_statMutex, TX_WAIT_FOREVER);
    size_t currentDepth = GetQueueSize();
    if (currentDepth > m_queueDepthMaxWindow) m_queueDepthMaxWindow = currentDepth;
    if (currentDepth > m_queueDepthMaxAll) m_queueDepthMaxAll = currentDepth;
    tx_mutex_put(&m_statMutex);
#endif

    return true;
}

//----------------------------------------------------------------------------
// Process (Static Entry Point)
//----------------------------------------------------------------------------
void ThreadXThread::Process(ULONG /*instance*/)
{
    ThreadXThread* thread = nullptr;
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(GetInstanceRegistryLock());
        auto it = GetInstanceRegistry().find(tx_thread_identify());
        if (it != GetInstanceRegistry().end())
            thread = it->second;
    }

    ASSERT_TRUE(thread != nullptr);
    thread->Run();
}

//----------------------------------------------------------------------------
// WatchdogCheck
//----------------------------------------------------------------------------
void ThreadXThread::WatchdogCheck()
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
void ThreadXThread::ThreadCheck()
{
    m_lastAliveTime.store(Timer::GetNow());
}

//----------------------------------------------------------------------------
// WatchdogCheckAll
//----------------------------------------------------------------------------
void ThreadXThread::WatchdogCheckAll()
{
    const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
    ThreadXThread* p = GetWatchdogHead();
    while (p != nullptr)
    {
        p->WatchdogCheck();
        p = p->m_watchdogNext;
    }
}

//----------------------------------------------------------------------------
// GetInstanceRegistry
//----------------------------------------------------------------------------
dmq::xmap<TX_THREAD*, ThreadXThread*>& ThreadXThread::GetInstanceRegistry()
{
    static dmq::xmap<TX_THREAD*, ThreadXThread*> registry;
    return registry;
}

//----------------------------------------------------------------------------
// GetInstanceRegistryLock
//----------------------------------------------------------------------------
dmq::RecursiveMutex& ThreadXThread::GetInstanceRegistryLock()
{
    static dmq::RecursiveMutex* lock = new dmq::RecursiveMutex();
    return *lock;
}

//----------------------------------------------------------------------------
// GetWatchdogHead
//----------------------------------------------------------------------------
ThreadXThread*& ThreadXThread::GetWatchdogHead()
{
    static ThreadXThread* head = nullptr;
    return head;
}

//----------------------------------------------------------------------------
// GetWatchdogLock
//----------------------------------------------------------------------------
dmq::RecursiveMutex& ThreadXThread::GetWatchdogLock()
{
    static dmq::RecursiveMutex* lock = new dmq::RecursiveMutex();
    return *lock;
}

//----------------------------------------------------------------------------
// Run (Member Function Loop)
//----------------------------------------------------------------------------
void ThreadXThread::Run()
{
    bool selfExit = false;
    m_selfExitPtr = &selfExit;

    ThreadMsg* msg = nullptr;
    while (!selfExit && !m_exit.load())
    {
        dmq::Duration timeout;
        {
            m_lastAliveTime.store(Timer::GetNow());
            timeout = m_watchdogTimeout.load();
        }

        // If watchdog active, use a finite timeout so we can periodically update 
        // m_lastAliveTime while idle. Otherwise, block forever to save power.
        ULONG waitOption = TX_WAIT_FOREVER;
        if (timeout.count() > 0)
        {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
            waitOption = (static_cast<ULONG>(ms / 4) * TX_TIMER_TICKS_PER_SECOND) / 1000;
            if (waitOption == 0) waitOption = 1;
        }

        msg = m_queue.Receive(waitOption);
        if (!msg) continue; // Timeout or other failure

        int msgId = msg->GetId();
        if (msgId == MSG_DISPATCH_DELEGATE)
        {
#if defined(DMQ_DATABUS_TOOLS)
            // Update latency stats before invoking
            dmq::Duration latency = Timer::GetNow() - msg->GetEnqueueTime();
            {
                tx_mutex_get(&m_statMutex, TX_WAIT_FOREVER);
                m_latencyTotalWindow += latency;
                m_latencyCountWindow++;
                if (latency > m_latencyMaxWindow) m_latencyMaxWindow = latency;
                if (latency > m_latencyMaxAll) m_latencyMaxAll = latency;
                m_dispatchCountAll++;
                tx_mutex_put(&m_statMutex);
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
                tx_mutex_get(&m_statMutex, TX_WAIT_FOREVER);
                m_invokeTotalWindow += invokeTime;
                m_invokeCountWindow++;
                if (invokeTime > m_invokeMaxWindow) m_invokeMaxWindow = invokeTime;
                if (invokeTime > m_invokeMaxAll) m_invokeMaxAll = invokeTime;
                tx_mutex_put(&m_statMutex);
            }
#endif
        }

        delete msg;

        if (msgId == MSG_EXIT_THREAD) {
            break;
        }
    }

    // Signal ExitThread() that the loop has exited
    tx_semaphore_put(&m_exitSem);
    m_selfExitPtr = nullptr;
}

#if defined(DMQ_DATABUS_TOOLS)
//----------------------------------------------------------------------------
// SnapshotStats
//----------------------------------------------------------------------------
ThreadXThread::ThreadStats ThreadXThread::SnapshotStats()
{
    tx_mutex_get(&m_statMutex, TX_WAIT_FOREVER);
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

    tx_mutex_put(&m_statMutex);
    return stats;
}
#endif

} // namespace dmq::os
