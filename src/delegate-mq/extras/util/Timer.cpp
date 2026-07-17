#include "Timer.h"
#include "Fault.h"
#include <chrono>
#include <algorithm>

namespace dmq::util {

using namespace std;

std::atomic<bool> Timer::m_timerStopped{false};

//------------------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------------------
Timer::Timer()
{
    const dmq::LockGuard<dmq::RecursiveMutex> lock(GetLock());
    m_enabled = false;
}

//------------------------------------------------------------------------------
// Destructor
//------------------------------------------------------------------------------
Timer::~Timer()
{
    const dmq::LockGuard<dmq::RecursiveMutex> lock(GetLock());
    
    // Remove 'this' from the intrusive linked list
    Timer** pp = &GetTimersHead();
    while (*pp != nullptr)
    {
        if (*pp == this)
        {
            *pp = this->m_next;
            this->m_next = nullptr;
            break;
        }
        pp = &((*pp)->m_next);
    }
}

//------------------------------------------------------------------------------
// Start
//------------------------------------------------------------------------------
void Timer::Start(dmq::Duration timeout, bool once)
{
    if (timeout <= dmq::Duration(0)) {
#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
        // Use the macro from Fault.h to halt the system
        ASSERT();
        return;
#else
        throw std::invalid_argument("Timeout cannot be 0");
#endif
    }

    const dmq::LockGuard<dmq::RecursiveMutex> lock(GetLock());

    m_timeout = timeout;
    m_once = once;
    m_expireTime = GetNow() + m_timeout;
    m_enabled = true;

    // Search for 'this' in the intrusive list
    bool found = false;
    Timer* p = GetTimersHead();
    while (p != nullptr)
    {
        if (p == this)
        {
            found = true;
            break;
        }
        p = p->m_next;
    }

    // Only add if not already in the list. 
    if (!found)
    {
        m_next = GetTimersHead();
        GetTimersHead() = this;
    }

    LOG_INFO("Timer::Start timeout={}", m_timeout.count());
}

//------------------------------------------------------------------------------
// Stop
//------------------------------------------------------------------------------
void Timer::Stop()
{
    const dmq::LockGuard<dmq::RecursiveMutex> lock(GetLock());

    m_enabled = false;

    // Don't remove immediately! Just set a flag.
    // Let ProcessTimers() handle the actual removal safely.
    m_timerStopped = true;

    LOG_INFO("Timer::Stop timeout={}", m_timeout.count());
}

//------------------------------------------------------------------------------
// CheckExpired
//------------------------------------------------------------------------------
bool Timer::CheckExpired()
{
    if (!m_enabled)
        return false;

    // Has the timer expired?
    if (GetNow() < m_expireTime)
        return false;     // Not expired yet

    if (m_once)
    {
        m_enabled = false;
        m_timerStopped = true;
    }
    else
    {
        // Increment the timer to the next expiration
        m_expireTime += m_timeout;

        // Check if we are still behind (timer starvation)
        // If the new deadline is STILL in the past, we are falling behind.
        if (GetNow() > m_expireTime)
        {
            // The timer has fallen behind so set time expiration further forward.
            m_expireTime = GetNow();

            // Timer processing is falling behind. Maybe user timer expiration is too 
            // short, time processing takings too long, or CheckExpired not called 
            // frequently enough. 
            LOG_INFO("Timer::CheckExpired Timer Processing Falling Behind");
        }
    }

    return true;
}

//------------------------------------------------------------------------------
// ProcessTimers
//------------------------------------------------------------------------------
void Timer::ProcessTimers()
{   
    dmq::Signal<void()>::Snapshot snapshots[dmq::MAX_TIMER_EXPIRED];
    size_t count = 0;

    {
        const dmq::LockGuard<dmq::RecursiveMutex> lock(GetLock());

        // Remove disabled timer from the list if stopped
        if (m_timerStopped)
        {
            Timer** pp = &GetTimersHead();
            while (*pp != nullptr)
            {
                if (!((*pp)->m_enabled))
                {
                    Timer* next = (*pp)->m_next;
                    (*pp)->m_next = nullptr;
                    *pp = next;
                }
                else
                {
                    pp = &((*pp)->m_next);
                }
            }
            m_timerStopped = false;
        }

        // Identify expired timers while holding the lock.
        // NOTE: Snapshots are captured under the lock to ensure the Timer
        // object is valid. The actual invocation happens outside the lock.
        Timer* t = GetTimersHead();
        while (t != nullptr)
        {
            if (count >= dmq::MAX_TIMER_EXPIRED) {
                LOG_ERROR("Timer::ProcessTimers MAX_TIMER_EXPIRED exceeded");
                ASSERT_TRUE(count < dmq::MAX_TIMER_EXPIRED);
                break;
            }

            if (t->CheckExpired())
            {
                snapshots[count++] = t->OnExpired.GetSnapshot();
            }
            t = t->m_next;
        }
    }

    // Call the client's expired callback functions outside the lock.
    // This allows callbacks to perform thread-safe operations (like DataBus::Publish)
    // without risking a deadlock with the global timer lock.
    // The Snapshot holds shared_ptrs to the delegates, so even if a Timer 
    // was deleted on another thread after the lock was released, the 
    // callback targets remain valid.
    for (size_t i = 0; i < count; ++i)
    {
        dmq::Signal<void()>::InvokeSnapshot(snapshots[i]);
        snapshots[i] = {};  // release shared_ptr refs so delegates aren't held between calls
    }
}

//------------------------------------------------------------------------------
// GetNow
//------------------------------------------------------------------------------
dmq::TimePoint Timer::GetNow()
{
    // time_point_cast converts the internal clock resolution (nanos) 
    // to your custom resolution (millis) inside the time_point wrapper.
    return std::chrono::time_point_cast<dmq::Duration>(dmq::Clock::now());
}

} // namespace dmq::util