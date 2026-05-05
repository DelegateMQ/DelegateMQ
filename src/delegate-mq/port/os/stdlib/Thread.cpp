#ifndef DMQ_THREAD_STDLIB
#error "port/os/stdlib/Thread.cpp requires DMQ_THREAD_STDLIB. Remove this file from your build configuration or define DMQ_THREAD_STDLIB."
#endif

#include "DelegateMQ.h"
#include "Thread.h"
#include "extras/util/Fault.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace dmq::os {

using namespace std;
using namespace dmq::util;

#define MSG_DISPATCH_DELEGATE	1
#define MSG_EXIT_THREAD			2

//----------------------------------------------------------------------------
// Thread
//----------------------------------------------------------------------------
Thread::Thread(const std::string& threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout, const std::string& cpuName)
    : m_thread(std::nullopt)
    , m_exit(false)
    , THREAD_NAME(threadName)
    , CPU_NAME(cpuName)
    , MAX_QUEUE_SIZE(maxQueueSize)
    , FULL_POLICY(fullPolicy)
    , m_dispatchTimeout(dispatchTimeout)
{
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
}

//----------------------------------------------------------------------------
// CreateThread
//----------------------------------------------------------------------------
bool Thread::CreateThread(std::optional<dmq::Duration> watchdogTimeout)
{
    if (!m_thread)
    {
        m_threadStartPromise.emplace();
        m_threadStartFuture.emplace(m_threadStartPromise->get_future());
        m_exit = false;

        m_thread.emplace(&Thread::Process, this);

        auto handle = m_thread->native_handle();
        SetThreadName(handle, THREAD_NAME);

        // Wait for the thread to enter the Process method
        m_threadStartFuture->get();

        m_lastAliveTime.store(Timer::GetNow());

        // Caller wants a watchdog timer?
        if (watchdogTimeout.has_value())
        {
            m_watchdogTimeout = watchdogTimeout.value();

            // Checks that Process() loop is iterating. Fires at half the watchdog
            // interval so a stall is detected within one full watchdogTimeout window.
            // WatchdogCheck runs synchronously in ProcessTimers() context — no dispatch,
            // so ProcessTimers() cannot stall waiting on this thread's queue.
            m_watchdogTimer = std::unique_ptr<Timer>(new Timer());
            m_watchdogTimerConn = m_watchdogTimer->OnExpired.Connect(MakeDelegate(this, &Thread::WatchdogCheck));
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
// GetThreadId
//----------------------------------------------------------------------------
std::thread::id Thread::GetThreadId()
{
    if (!m_thread.has_value())
        throw std::invalid_argument("Thread pointer is null");

    return m_thread->get_id();
}

//----------------------------------------------------------------------------
// GetCurrentThreadId
//----------------------------------------------------------------------------
std::thread::id Thread::GetCurrentThreadId()
{
    return this_thread::get_id();
}

//----------------------------------------------------------------------------
// IsCurrentThread
//----------------------------------------------------------------------------
bool Thread::IsCurrentThread()
{
    if (!m_thread.has_value())
        return false;

    return GetThreadId() == GetCurrentThreadId();
}

//----------------------------------------------------------------------------
// GetQueueSize
//----------------------------------------------------------------------------
size_t Thread::GetQueueSize()
{
    lock_guard<mutex> lock(m_mutex);
    return m_queue.size();
}

void Thread::Sleep(dmq::Duration timeout) {
    std::this_thread::sleep_for(timeout);
}

//----------------------------------------------------------------------------
// SetThreadName
//----------------------------------------------------------------------------
void Thread::SetThreadName(std::thread::native_handle_type handle, const std::string& name)
{
#ifdef _WIN32
    // Set the thread name so it shows in the Visual Studio Debug Location toolbar
    std::wstring wstr(name.begin(), name.end());
    HRESULT hr = SetThreadDescription(handle, wstr.c_str());
    if (FAILED(hr))
    {
        // Handle error if needed
    }
#endif
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void Thread::ExitThread()
{
    if (!m_thread)
        return;

    if (m_watchdogTimer)
    {
        m_watchdogTimer->Stop();
        m_watchdogTimerConn.Disconnect();
    }

    // Create a new ThreadMsg
    auto threadMsg = xmake_shared<ThreadMsg>(MSG_EXIT_THREAD, nullptr);

    {
        lock_guard<mutex> lock(m_mutex);

        // Set exit flag INSIDE lock before notifying.
        // This ensures that when a blocked producer wakes up, it sees m_exit == true immediately.
        m_exit.store(true);

        // Explicitly allow Exit message to bypass the MAX_QUEUE_SIZE limit.
        // We do not wait on m_cvNotFull here to prevent deadlock during shutdown.
        m_queue.push(threadMsg);

        // Wake up consumers
        m_cv.notify_one();
        // Wake up blocked producers (DispatchDelegate)
        m_cvNotFull.notify_all();
    }

    // Prevent deadlock if ExitThread is called from within the thread itself
    if (m_thread->joinable())
    {
        if (std::this_thread::get_id() != m_thread->get_id())
        {
            m_thread->join();
        }
        else
        {
            // We are killing ourselves. Detach so the thread object cleans up naturally.
            m_thread->detach();
        }
    }

    {
        lock_guard<mutex> lock(m_mutex);
        m_thread.reset();
        while (!m_queue.empty())
            m_queue.pop();

        // Final cleanup notification
        m_cvNotFull.notify_all();
    }
}

//----------------------------------------------------------------------------
// DispatchDelegate
//----------------------------------------------------------------------------
bool Thread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    // Early check, though we re-check inside lock for safety
    if (m_exit.load())
        return false;

    if (!m_thread.has_value())
        throw std::invalid_argument("Thread pointer is null");

    std::unique_lock<std::mutex> lk(m_mutex);

    // [BACK PRESSURE / DROP / FAULT / TIMEOUT LOGIC]
    if (MAX_QUEUE_SIZE > 0 && m_queue.size() >= MAX_QUEUE_SIZE)
    {
        if (FULL_POLICY == FullPolicy::DROP)
            return false;  // silently discard — caller is not stalled, no allocation wasted

        if (FULL_POLICY == FullPolicy::FAULT)
        {
            printf("[Thread] CRITICAL: Queue full on thread '%s'! TRIGGERING FAULT.\n", THREAD_NAME.c_str());
            ASSERT_TRUE(false);
            return false;
        }

        if (FULL_POLICY == FullPolicy::TIMEOUT)
        {
            bool hasSpace = m_cvNotFull.wait_for(lk, m_dispatchTimeout, [this]() {
                return m_queue.size() < MAX_QUEUE_SIZE || m_exit.load();
            });
            if (!hasSpace) {
                printf("[Thread] WARNING: Queue post timed out on '%s' — possible deadlock. Message dropped.\n", THREAD_NAME.c_str());
                return false;
            }
            // space found — fall through to push
        }

    }

    // If using XALLOCATOR explicit operator new required. See xallocator.h.
    auto threadMsg = xmake_shared<ThreadMsg>(MSG_DISPATCH_DELEGATE, msg);
#if defined(DMQ_DATABUS_TOOLS)
    threadMsg->SetEnqueueTime(Timer::GetNow());
#endif

    // If we woke up because of exit (or exit happened while waiting), abort
    if (m_exit.load())
        return false;

    m_queue.push(threadMsg);

#if defined(DMQ_DATABUS_TOOLS)
    // Update monitoring stats
    size_t currentDepth = m_queue.size();
    if (currentDepth > m_queueDepthMaxWindow) m_queueDepthMaxWindow = currentDepth;
    if (currentDepth > m_queueDepthMaxAll) m_queueDepthMaxAll = currentDepth;
#endif

    m_cv.notify_one();

    return true;
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
// WatchdogCheck
//----------------------------------------------------------------------------
void Thread::WatchdogCheck()
{
    auto now = Timer::GetNow();
    auto lastAlive = m_lastAliveTime.load();

    auto delta = now - lastAlive;

    // Watchdog expired?
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
// Process
//----------------------------------------------------------------------------
void Thread::Process()
{
    // Signal that the thread has started processing to notify CreateThread
    m_threadStartPromise->set_value();

    while (1)
    {
        dmq::Duration watchdogTimeout;
        {
            m_lastAliveTime.store(Timer::GetNow());
            watchdogTimeout = m_watchdogTimeout.load();
        }

        std::shared_ptr<ThreadMsg> msg;
        {
            std::unique_lock<std::mutex> lk(m_mutex);

            // Wait for message to be added to the queue.
            // If watchdog active, use a finite timeout so we can periodically update 
            // m_lastAliveTime while idle. Otherwise, block forever.
            auto predicate = [this]() { return !m_queue.empty() || m_exit.load(); };
            if (watchdogTimeout.count() > 0)
            {
                // Wake up frequently to ensure heartbeat is updated while idle
                m_cv.wait_for(lk, watchdogTimeout / 10, predicate);
            }
            else
            {
                m_cv.wait(lk, predicate);
            }

            // Always update alive time immediately after waking up
            m_lastAliveTime.store(Timer::GetNow());

            // If empty and exit is true, we should exit.
            if (m_queue.empty())
            {
                if (m_exit.load()) return;
                continue;
            }

            // Get highest priority message within queue
            msg = m_queue.top();
            m_queue.pop();

            // Unblock producers now that space is available
            if (MAX_QUEUE_SIZE > 0)
            {
                m_cvNotFull.notify_one();
            }
        }

        switch (msg->GetId())
        {
            case MSG_DISPATCH_DELEGATE:
            {
#if defined(DMQ_DATABUS_TOOLS)
                // Update latency stats before invoking
                dmq::Duration latency = Timer::GetNow() - msg->GetEnqueueTime();
                {
                    lock_guard<mutex> lock(m_mutex);
                    m_latencyTotalWindow += latency;
                    m_latencyCountWindow++;
                    if (latency > m_latencyMaxWindow) m_latencyMaxWindow = latency;
                    if (latency > m_latencyMaxAll) m_latencyMaxAll = latency;
                    m_dispatchCountAll++;
                }
#endif

                auto delegateMsg = msg->GetData();
                if (delegateMsg) {
                    auto invoker = delegateMsg->GetInvoker();
                    if (invoker) {
                        invoker->Invoke(delegateMsg);
                    }
                }
                break;
            }

            case MSG_EXIT_THREAD:
            {
                return;
            }

            default:
            {
                throw std::invalid_argument("Invalid message ID");
            }
        }
    }
}

#if defined(DMQ_DATABUS_TOOLS)
//----------------------------------------------------------------------------
// SnapshotStats
//----------------------------------------------------------------------------
Thread::ThreadStats Thread::SnapshotStats()
{
    lock_guard<mutex> lock(m_mutex);
    ThreadStats stats;
    stats.cpu_name = CPU_NAME;
    stats.thread_name = THREAD_NAME;
    stats.queue_depth = m_queue.size();
    stats.queue_depth_max_window = m_queueDepthMaxWindow;
    stats.queue_depth_max_all = m_queueDepthMaxAll;
    stats.queue_size_limit = MAX_QUEUE_SIZE;
    
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
    m_latencyTotalWindow = dmq::Duration(0);
    m_latencyCountWindow = 0;
    m_latencyMaxWindow = dmq::Duration(0);

    return stats;
}
#endif

} // namespace dmq::os
