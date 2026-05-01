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
Thread::Thread(const std::string& threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout)
    : m_thread(std::nullopt)
    , m_exit(false)
    , THREAD_NAME(threadName)
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
            // Create watchdog timer
            m_watchdogTimeout = watchdogTimeout.value();

            // Timer to ensure the Thread instance runs periodically.
            m_threadTimer = std::unique_ptr<Timer>(new Timer());
            m_threadTimerConn = m_threadTimer->OnExpired.Connect(MakeDelegate(this, &Thread::ThreadCheck, *this));
            m_threadTimer->Start(m_watchdogTimeout.load() / 4);

            // Timer to check that this Thread instance runs. 
            m_watchdogTimer = std::unique_ptr<Timer>(new Timer());
            m_watchdogTimerConn = m_watchdogTimer->OnExpired.Connect(MakeDelegate(this, &Thread::WatchdogCheck));
            m_watchdogTimer->Start(m_watchdogTimeout.load() / 2);
        }

        LOG_INFO("Thread::CreateThread {}", THREAD_NAME);
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

    if (m_threadTimer)
    {
        m_threadTimer->Stop();
        m_threadTimerConn.Disconnect();
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

    LOG_INFO("Thread::ExitThread {}", THREAD_NAME);
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

    // If we woke up because of exit (or exit happened while waiting), abort
    if (m_exit.load())
        return false;

    m_queue.push(threadMsg);
    m_cv.notify_one();

    LOG_INFO("Thread::DispatchDelegate\n   thread={}\n   target={}",
        THREAD_NAME,
        typeid(*threadMsg->GetData()->GetInvoker()).name());

    return true;
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
        printf("[Thread] Watchdog detected unresponsive thread: %s\n", THREAD_NAME.c_str());
        LOG_ERROR("Watchdog detected unresponsive thread: {}", THREAD_NAME);

        // @TODO Optionally trigger recovery, restart, or further actions here
        // For example, throw or notify external system
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
// Process
//----------------------------------------------------------------------------
void Thread::Process()
{
    // Signal that the thread has started processing to notify CreateThread
    m_threadStartPromise->set_value();

    LOG_INFO("Thread::Process Start {}", THREAD_NAME);

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
                m_cv.wait_for(lk, watchdogTimeout / 4, predicate);
            }
            else
            {
                m_cv.wait(lk, predicate);
            }

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
                // @TODO: Update error handling below if necessary 

                auto delegateMsg = msg->GetData();
                ASSERT_TRUE(delegateMsg);

                auto invoker = delegateMsg->GetInvoker();
                ASSERT_TRUE(invoker);

                // Invoke the delegate destination target function
                bool success = invoker->Invoke(delegateMsg);
                ASSERT_TRUE(success);
                break;
            }

            case MSG_EXIT_THREAD:
            {
                LOG_INFO("Thread::Process Exit Thread {}", THREAD_NAME);
                return;
            }

            default:
            {
                LOG_INFO("Thread::Process Invalid Message {}", THREAD_NAME);
                throw std::invalid_argument("Invalid message ID");
            }
        }
    }
}

} // namespace dmq::os

