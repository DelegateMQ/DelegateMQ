#ifndef _WIN32
#error "port/os/win32/Thread.cpp is Windows-only and must not be compiled on non-Windows targets. Remove this file from your build configuration."
#endif

#include "DelegateMQ.h"
#include "Thread.h"
#include "extras/util/Fault.h"
#include <iostream>

namespace dmq::os {

using namespace dmq::util;

#define MSG_DISPATCH_DELEGATE    1
#define MSG_EXIT_THREAD          2

// Thread-local pointer into Process()'s stack frame. Set non-null only while
// Process() is running on a given thread. ExitThread() writes true through it
// when the thread destroys its own owning object so Process() can exit without
// touching freed members.
static thread_local bool* t_self_exit = nullptr;

//----------------------------------------------------------------------------
// Thread
//----------------------------------------------------------------------------
Thread::Thread(const char* threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout, const char* cpuName)
    : THREAD_NAME(threadName)
    , CPU_NAME(cpuName)
    , MAX_QUEUE_SIZE(maxQueueSize)
    , FULL_POLICY(fullPolicy)
    , m_dispatchTimeout(dispatchTimeout)
    , m_exit(false)
{
    InitializeCriticalSection(&m_cs);
    InitializeConditionVariable(&m_cvNotEmpty);
    InitializeConditionVariable(&m_cvNotFull);
}

//----------------------------------------------------------------------------
// ~Thread
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

    DeleteCriticalSection(&m_cs);
}

//----------------------------------------------------------------------------
// CreateThread
//----------------------------------------------------------------------------
bool Thread::CreateThread(std::optional<dmq::Duration> watchdogTimeout)
{
    if (m_hThread == NULL)
    {
        m_exit = false;

        // Manual-reset event for startup synchronization
        m_hStartEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

        m_hThread = ::CreateThread(NULL, 0, ThreadProc, this, 0, &m_threadId);
        if (m_hThread == NULL) return false;

        // Set the thread name so it shows in the Visual Studio Debug Location toolbar
        std::wstring wname(THREAD_NAME.begin(), THREAD_NAME.end());
        SetThreadDescription(m_hThread, wname.c_str());

        // Wait for the thread to enter the Process method
        WaitForSingleObject(m_hStartEvent, INFINITE);
        CloseHandle(m_hStartEvent);
        m_hStartEvent = NULL;

        m_lastAliveTime.store(Timer::GetNow());

        // Caller wants a watchdog timer?
        if (watchdogTimeout.has_value())
        {
            m_watchdogTimeout = watchdogTimeout.value();

            // Add to watchdog registry if not already present
            {
                dmq::LockGuard<dmq::RecursiveMutex> lock(GetWatchdogLock());
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
    }
    return true;
}

//----------------------------------------------------------------------------
// ThreadProc
//----------------------------------------------------------------------------
DWORD WINAPI Thread::ThreadProc(LPVOID lpParam)
{
    static_cast<Thread*>(lpParam)->Process();
    return 0;
}

//----------------------------------------------------------------------------
// DispatchDelegate
//----------------------------------------------------------------------------
bool Thread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    if (m_exit.load() || m_hThread == NULL) return false;

    EnterCriticalSection(&m_cs);

    // [BACK PRESSURE / DROP / FAULT / TIMEOUT LOGIC]
    if (MAX_QUEUE_SIZE > 0 && (m_highQueue.size() + m_normalQueue.size()) >= MAX_QUEUE_SIZE)
    {
        if (FULL_POLICY == FullPolicy::DROP)
        {
            LeaveCriticalSection(&m_cs);
            return false; // silently discard
        }

        if (FULL_POLICY == FullPolicy::FAULT)
        {
            LeaveCriticalSection(&m_cs);
            printf("[Thread] CRITICAL: Queue full on thread '%s'! TRIGGERING FAULT.\n", THREAD_NAME.c_str());
            ASSERT_TRUE(false);
            return false;
        }

        if (FULL_POLICY == FullPolicy::TIMEOUT)
        {
            DWORD dwTimeout = static_cast<DWORD>(
                std::chrono::duration_cast<std::chrono::milliseconds>(m_dispatchTimeout).count());
            while ((m_highQueue.size() + m_normalQueue.size()) >= MAX_QUEUE_SIZE && !m_exit.load())
            {
                if (!SleepConditionVariableCS(&m_cvNotFull, &m_cs, dwTimeout))
                {
                    LeaveCriticalSection(&m_cs);
                    printf("[Thread] WARNING: Queue post timed out on '%s' — possible deadlock. Message dropped.\n", THREAD_NAME.c_str());
                    return false;
                }
            }
        }
    }

    // If using XALLOCATOR explicit operator new required. See xallocator.h.
    auto threadMsg = xmake_shared<ThreadMsg>(MSG_DISPATCH_DELEGATE, msg);
    #if defined(DMQ_DATABUS_TOOLS)
    threadMsg->SetEnqueueTime(Timer::GetNow());
    #endif

    // If we woke up because of exit (or exit happened while waiting), abort
    if (!m_exit.load())
    {
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

        WakeConditionVariable(&m_cvNotEmpty);
    }
    else
    {
        LeaveCriticalSection(&m_cs);
        return false;
    }

    LeaveCriticalSection(&m_cs);
    return true;
}

//----------------------------------------------------------------------------
// Process
//----------------------------------------------------------------------------
void Thread::Process()
{
    // selfExit is set by ExitThread() when the thread destroys its own owner.
    // It lives on this stack frame so it remains valid even after 'this' is freed.
    bool selfExit = false;
    t_self_exit = &selfExit;

    // Signal that the thread has started processing to notify CreateThread
    SetEvent(m_hStartEvent);

    while (!selfExit)
    {
        dmq::Duration watchdogTimeout;
        {
            m_lastAliveTime.store(Timer::GetNow());
            watchdogTimeout = m_watchdogTimeout.load();
        }

        std::shared_ptr<ThreadMsg> msg;

        EnterCriticalSection(&m_cs);

        // Wait for message to be added to the queue.
        // If watchdog active, use a finite timeout so we can periodically update 
        // m_lastAliveTime while idle. Otherwise, block forever.
        DWORD dwTimeout = INFINITE;
        if (watchdogTimeout.count() > 0)
        {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(watchdogTimeout).count();
            dwTimeout = static_cast<DWORD>(ms / 4);
            if (dwTimeout == 0) dwTimeout = 1;
        }

        while ((m_highQueue.empty() && m_normalQueue.empty()) && !m_exit.load())
        {
            if (!SleepConditionVariableCS(&m_cvNotEmpty, &m_cs, dwTimeout))
            {
                if (GetLastError() == ERROR_TIMEOUT)
                    break; // Timeout reached, break inner while to update m_lastAliveTime
            }
        }

        // If empty and exit is true, we should exit.
        if ((m_highQueue.empty() && m_normalQueue.empty()) && m_exit.load())
        {
            LeaveCriticalSection(&m_cs);
            break;
        }

        // If queue still empty, it means we timed out. Loop again to update m_lastAliveTime.
        if ((m_highQueue.empty() && m_normalQueue.empty()))
        {
            LeaveCriticalSection(&m_cs);
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
            WakeConditionVariable(&m_cvNotFull);

        LeaveCriticalSection(&m_cs);

        switch (msg->GetId())
        {
            case MSG_DISPATCH_DELEGATE:
            {
#if defined(DMQ_DATABUS_TOOLS)
                // Update latency stats before invoking
                dmq::Duration latency = Timer::GetNow() - msg->GetEnqueueTime();
                {
                    EnterCriticalSection(&m_cs);
                    m_latencyTotalWindow += latency;
                    m_latencyCountWindow++;
                    if (latency > m_latencyMaxWindow) m_latencyMaxWindow = latency;
                    if (latency > m_latencyMaxAll) m_latencyMaxAll = latency;
                    m_dispatchCountAll++;
                    LeaveCriticalSection(&m_cs);
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
                try {
                    bool success = invoker->Invoke(delegateMsg);
                    if (!selfExit) ASSERT_TRUE(success);
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
                    std::cerr << "[Thread:" << THREAD_NAME << "] Unhandled exception in delegate callback: " << e.what() << std::endl;
                    ASSERT();
                }
                catch (...) {
                    std::cerr << "[Thread:" << THREAD_NAME << "] Unhandled unknown exception in delegate callback." << std::endl;
                    ASSERT();
                }
#else
                bool success = invoker->Invoke(delegateMsg);
                if (!selfExit) ASSERT_TRUE(success);
#endif
                if (selfExit) {
                    t_self_exit = nullptr;
                    return;
                }
#if defined(DMQ_DATABUS_TOOLS)
                dmq::Duration invokeTime = Timer::GetNow() - start;
                if (!selfExit) {
                    EnterCriticalSection(&m_cs);
                    m_invokeTotalWindow += invokeTime;
                    m_invokeCountWindow++;
                    if (invokeTime > m_invokeMaxWindow) m_invokeMaxWindow = invokeTime;
                    if (invokeTime > m_invokeMaxAll) m_invokeMaxAll = invokeTime;
                    LeaveCriticalSection(&m_cs);
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
                break;
            }
        }
    }
    t_self_exit = nullptr;
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void Thread::ExitThread()
{
    if (m_hThread == NULL) return;

    EnterCriticalSection(&m_cs);

    // Set exit flag INSIDE lock before notifying.
    // This ensures that when a blocked producer wakes up, it sees m_exit == true immediately.
    m_exit.store(true);

    // Explicitly allow Exit message to bypass the MAX_QUEUE_SIZE limit.
    // We do not wait on m_cvNotFull here to prevent deadlock during shutdown.
    m_highQueue.push_back(xmake_shared<ThreadMsg>(MSG_EXIT_THREAD, nullptr));

    // Wake up consumers
    WakeConditionVariable(&m_cvNotEmpty);

    // Wake up blocked producers (DispatchDelegate)
    WakeAllConditionVariable(&m_cvNotFull);

    LeaveCriticalSection(&m_cs);

    // Prevent deadlock if ExitThread is called from within the thread itself
    if (::GetCurrentThreadId() != m_threadId)
    {
        WaitForSingleObject(m_hThread, INFINITE);
    }
    else
    {
        if (t_self_exit) *t_self_exit = true;
    }

    CloseHandle(m_hThread);
    m_hThread = NULL;
}

//----------------------------------------------------------------------------
// GetThreadId
//----------------------------------------------------------------------------
DWORD Thread::GetThreadId() { return m_threadId; }

//----------------------------------------------------------------------------
// GetCurrentThreadId
//----------------------------------------------------------------------------
DWORD Thread::GetCurrentThreadId() { return ::GetCurrentThreadId(); }

//----------------------------------------------------------------------------
// IsCurrentThread
//----------------------------------------------------------------------------
bool Thread::IsCurrentThread() { return GetThreadId() == GetCurrentThreadId(); }

//----------------------------------------------------------------------------
// GetQueueSize
//----------------------------------------------------------------------------
size_t Thread::GetQueueSize()
{
    EnterCriticalSection(&m_cs);
    size_t size = (m_highQueue.size() + m_normalQueue.size());
    LeaveCriticalSection(&m_cs);
    return size;
}

void Thread::Sleep(dmq::Duration timeout) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    ::Sleep(static_cast<DWORD>(ms));
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
// WatchdogCheck
//----------------------------------------------------------------------------
void Thread::WatchdogCheck()
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
void Thread::ThreadCheck()
{
    m_lastAliveTime.store(Timer::GetNow());
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

#if defined(DMQ_DATABUS_TOOLS)
//----------------------------------------------------------------------------
// SnapshotStats
//----------------------------------------------------------------------------
Thread::ThreadStats Thread::SnapshotStats()
{
    EnterCriticalSection(&m_cs);
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

    LeaveCriticalSection(&m_cs);
    return stats;
}
#endif

} // namespace dmq::os
