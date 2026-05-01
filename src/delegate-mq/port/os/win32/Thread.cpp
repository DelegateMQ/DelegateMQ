#ifndef _WIN32
#error "port/os/win32/Thread.cpp is Windows-only and must not be compiled on non-Windows targets. Remove this file from your build configuration."
#endif

namespace dmq::os {

using namespace dmq::util;

#define MSG_DISPATCH_DELEGATE    1
#define MSG_EXIT_THREAD          2

//----------------------------------------------------------------------------
// Thread
//----------------------------------------------------------------------------
Thread::Thread(const std::string& threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout)
    : THREAD_NAME(threadName)
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
    if (MAX_QUEUE_SIZE > 0 && m_queue.size() >= MAX_QUEUE_SIZE)
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
            while (m_queue.size() >= MAX_QUEUE_SIZE && !m_exit.load())
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

    // If we woke up because of exit (or exit happened while waiting), abort
    if (!m_exit.load())
    {
        m_queue.push(threadMsg);
        WakeConditionVariable(&m_cvNotEmpty);
    }
    else
    {
        LeaveCriticalSection(&m_cs);
        return false;
    }

    LeaveCriticalSection(&m_cs);

    if (threadMsg)
    {
        LOG_INFO("Thread::DispatchDelegate\n   thread={}\n   target={}",
            THREAD_NAME,
            typeid(*threadMsg->GetData()->GetInvoker()).name());
    }
    return true;
}

//----------------------------------------------------------------------------
// Process
//----------------------------------------------------------------------------
void Thread::Process()
{
    // Signal that the thread has started processing to notify CreateThread
    SetEvent(m_hStartEvent);

    LOG_INFO("Thread::Process Start {}", THREAD_NAME);

    while (true)
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

        while (m_queue.empty() && !m_exit.load())
        {
            if (!SleepConditionVariableCS(&m_cvNotEmpty, &m_cs, dwTimeout))
            {
                if (GetLastError() == ERROR_TIMEOUT)
                    break; // Timeout reached, break inner while to update m_lastAliveTime
            }
        }

        // If empty and exit is true, we should exit.
        if (m_queue.empty() && m_exit.load())
        {
            LeaveCriticalSection(&m_cs);
            break;
        }

        // If queue still empty, it means we timed out. Loop again to update m_lastAliveTime.
        if (m_queue.empty())
        {
            LeaveCriticalSection(&m_cs);
            continue;
        }

        // Get highest priority message within queue
        msg = m_queue.top();
        m_queue.pop();

        // Unblock producers now that space is available
        if (MAX_QUEUE_SIZE > 0)
            WakeConditionVariable(&m_cvNotFull);

        LeaveCriticalSection(&m_cs);

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
                ASSERT_TRUE(false);
                break;
            }
        }
    }
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void Thread::ExitThread()
{
    if (m_hThread == NULL) return;

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

    EnterCriticalSection(&m_cs);

    // Set exit flag INSIDE lock before notifying.
    // This ensures that when a blocked producer wakes up, it sees m_exit == true immediately.
    m_exit.store(true);

    // Explicitly allow Exit message to bypass the MAX_QUEUE_SIZE limit.
    // We do not wait on m_cvNotFull here to prevent deadlock during shutdown.
    m_queue.push(xmake_shared<ThreadMsg>(MSG_EXIT_THREAD, nullptr));

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

    CloseHandle(m_hThread);
    m_hThread = NULL;

    LOG_INFO("Thread::ExitThread {}", THREAD_NAME);
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
    size_t size = m_queue.size();
    LeaveCriticalSection(&m_cs);
    return size;
}

void Thread::Sleep(dmq::Duration timeout) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    ::Sleep(static_cast<DWORD>(ms));
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

} // namespace dmq::os

