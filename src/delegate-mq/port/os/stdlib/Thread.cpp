#ifndef DMQ_THREAD_STDLIB
#error "port/os/stdlib/Thread.cpp requires DMQ_THREAD_STDLIB. Remove this file from your build configuration or define DMQ_THREAD_STDLIB."
#endif

#include "DelegateMQ.h"
#include "Thread.h"
#include "extras/util/Fault.h"

#ifdef _WIN32
#include <Windows.h>
#endif

// Thread-local pointer into Process()'s stack frame. Set non-null only while
// Process() is running on a given thread. ExitThread() writes true through it
// when the thread destroys its own owning object so Process() can exit without
// touching freed members.
static thread_local bool* t_self_exit = nullptr;

namespace dmq::os {

using namespace std;
using namespace dmq::util;

#define MSG_DISPATCH_DELEGATE	1
#define MSG_EXIT_THREAD			2

//----------------------------------------------------------------------------
// Thread
//----------------------------------------------------------------------------
Thread::Thread(const char* threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout, const char* cpuName)
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
    return (m_highQueue.size() + m_normalQueue.size());
}

void Thread::Sleep(dmq::Duration timeout) {
    std::this_thread::sleep_for(timeout);
}

//----------------------------------------------------------------------------
// SetThreadName
//----------------------------------------------------------------------------
void Thread::SetThreadName(std::thread::native_handle_type handle, const dmq::xstring& name)
{
    (void)handle;
    (void)name;
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

    // Create a new ThreadMsg
    auto threadMsg = xmake_shared<ThreadMsg>(MSG_EXIT_THREAD, nullptr);

    {
        lock_guard<mutex> lock(m_mutex);

        // Set exit flag INSIDE lock before notifying.
        // This ensures that when a blocked producer wakes up, it sees m_exit == true immediately.
        m_exit.store(true);

        // Explicitly allow Exit message to bypass the MAX_QUEUE_SIZE limit.
        // We do not wait on m_cvNotFull here to prevent deadlock during shutdown.
        if (threadMsg->GetPriority() == dmq::Priority::HIGH)
            m_highQueue.push_back(threadMsg);
        else
            m_normalQueue.push_back(threadMsg);

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
            // Signal Process() (running on this thread) to exit without touching
            // 'this' again — the owning object is about to be freed.
            if (t_self_exit) *t_self_exit = true;
        }
    }

    {
        lock_guard<mutex> lock(m_mutex);
        m_thread.reset();
        m_highQueue.clear();
        m_normalQueue.clear();

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
    if (MAX_QUEUE_SIZE > 0 && (m_highQueue.size() + m_normalQueue.size()) >= MAX_QUEUE_SIZE)
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
                return (m_highQueue.size() + m_normalQueue.size()) < MAX_QUEUE_SIZE || m_exit.load();
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

    if (threadMsg->GetPriority() == dmq::Priority::HIGH)
        m_highQueue.push_back(threadMsg);
    else
        m_normalQueue.push_back(threadMsg);

#if defined(DMQ_DATABUS_TOOLS)
    // Snapshot size while holding m_mutex
    size_t currentDepth = (m_highQueue.size() + m_normalQueue.size());
#endif

    m_cv.notify_one();
    lk.unlock(); // Release producer-blocking lock early

#if defined(DMQ_DATABUS_TOOLS)
    // Update monitoring stats under the separate stats mutex
    {
        lock_guard<mutex> lock(m_statsMutex);
        if (currentDepth > m_queueDepthMaxWindow) m_queueDepthMaxWindow = currentDepth;
        if (currentDepth > m_queueDepthMaxAll) m_queueDepthMaxAll = currentDepth;
    }
#endif

    return true;
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
    m_threadStartPromise->set_value();

    while (!selfExit)
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
            auto predicate = [this]() { return !(m_highQueue.empty() && m_normalQueue.empty()) || m_exit.load(); };
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
            if ((m_highQueue.empty() && m_normalQueue.empty()))
            {
                if (m_exit.load()) { t_self_exit = nullptr; return; }
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
                    lock_guard<mutex> lock(m_statsMutex);
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
                            lock_guard<mutex> lock(m_statsMutex);
                            m_invokeTotalWindow += invokeTime;
                            m_invokeCountWindow++;
                            if (invokeTime > m_invokeMaxWindow) m_invokeMaxWindow = invokeTime;
                            if (invokeTime > m_invokeMaxAll) m_invokeMaxAll = invokeTime;
                        }
#endif
                    }
                }
                break;
            }

            case MSG_EXIT_THREAD:
            {
                t_self_exit = nullptr;
                return;
            }

            default:
                ASSERT();
                break;
        }
        // msg goes out of scope here — may trigger self-destruction of 'this'.
        // After this point do not access any member; check selfExit in while().
    }
    t_self_exit = nullptr;
}

#if defined(DMQ_DATABUS_TOOLS)
//----------------------------------------------------------------------------
// SnapshotStats
//----------------------------------------------------------------------------
Thread::ThreadStats Thread::SnapshotStats()
{
    // Need m_mutex only for (m_highQueue.size() + m_normalQueue.size())
    size_t currentDepth = GetQueueSize();

    lock_guard<mutex> lock(m_statsMutex);
    ThreadStats stats;
    stats.cpu_name = CPU_NAME;
    stats.thread_name = THREAD_NAME;
    stats.queue_depth = currentDepth;
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

    return stats;
}
#endif

} // namespace dmq::os
