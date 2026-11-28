#ifndef DMQ_ONE_SHOT_TIMER_H
#define DMQ_ONE_SHOT_TIMER_H

/// @file OneShotTimer.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief "Run this on my thread once, in 200 ms": a one-shot timer in one
/// member, for timeouts, retries and debounce.
///
/// @code
/// class Door {
///     dmq::os::Thread m_thread{"Door"};
///     dmq::util::OneShotTimer m_autoClose{&m_thread};    // callbacks on m_thread
///
///     void OnOpened() { m_autoClose.Start(5s, dmq::MakeDelegate(this, &Door::Close)); }
///     void OnMotion() { m_autoClose.Start(5s); }          // restart: debounce
///     void OnClosed() { m_autoClose.Stop(); }
/// };
/// @endcode
///
/// The same with a dmq::util::Timer takes the timer, a ScopedConnection that
/// must be stored, and an async delegate, and a callback already queued to the
/// thread still runs after Stop() or a restart, so each handler re-checks the
/// time. OneShotTimer does that check itself:
///
/// - The callback runs at most once per Start(), on the thread given to the
///   constructor (none: the thread that calls Timer::ProcessTimers()).
/// - After Stop(), or a Start() that restarted the countdown, the earlier
///   expiry never runs the callback, even if it was already queued.
/// - Start() again before it fires restarts the countdown (debounce).
///   Start(timeout) keeps the callback; Start(timeout, callback) replaces it.
/// - Each expiry carries the generation of the Start() it belongs to; a
///   Start() or Stop() since then bumps it, and the callback runs only if the
///   generation still matches and the deadline has passed.
/// - Destroying the OneShotTimer stops it. A callback already queued then
///   finds it stopped and doesn't run; the state it checks is shared with the
///   queued call, so nothing touches the destroyed timer.
/// - Built on dmq::util::Timer: something must call Timer::ProcessTimers(),
///   as for any timer. Start()/Stop() take a dmq::RecursiveMutex: call them
///   from a thread, not an interrupt. The callback runs holding it, so it may
///   call Start() or Stop() itself, and another thread's Start()/Stop() waits
///   for a callback that is running.
/// - Allocates like the rest of the library: Start(timeout, callback) copies
///   the callback, and each expiry is an async call to the thread. Start(timeout)
///   and Stop() don't allocate.
/// - Thread ports only (it dispatches to a thread); not with DMQ_THREAD_NONE.

#include "delegate/DelegateOpt.h"
#include "delegate/Delegate.h"
#include "delegate/DelegateAsync.h"
#include "delegate/UnicastDelegate.h"
#include "extras/util/Timer.h"
#include <cstdint>
#include <memory>

#if !defined(DMQ_HAS_THREADING)
    #error "OneShotTimer dispatches its callback to a thread: it needs a thread port (DMQ_THREAD_*)"
#endif

namespace dmq::util {

namespace detail {

/// The timer's state, shared between the OneShotTimer and the expiries it
/// queues, so a queued expiry outlives a destroyed timer harmlessly.
struct OneShotState : std::enable_shared_from_this<OneShotState>
{
    XALLOCATOR

    dmq::RecursiveMutex lock;               // recursive: the callback may call Start()/Stop()
    dmq::IThread* thread = nullptr;         // where the callback runs; nullptr: the timer thread
    uint32_t generation = 0;                // bumped by every Start() and Stop()
    bool armed = false;                     // Start()ed and not yet fired or stopped
    dmq::TimePoint deadline{};              // the earliest time this Start() may fire
    dmq::UnicastDelegate<void()> callback;
    bool firing = false;                    // the callback is running (moved out of `callback`)

    /// The timer thread, on each expiry (outside Timer's locks): stamp it with
    /// the current generation and hand it to the callback's thread.
    void OnTick()
    {
        uint32_t gen = 0;
        dmq::IThread* target = nullptr;
        {
            dmq::LockGuard<dmq::RecursiveMutex> guard(lock);
            if (!armed)
                return;
            gen = generation;
            target = thread;
        }
        if (target == nullptr) {
            OnExpired(gen);
            return;
        }
        // Holds the state for the queued call; it checks for itself whether
        // it still belongs to the current Start()
        dmq::MakeDelegate(shared_from_this(), &OneShotState::OnExpired, *target)(gen);
    }

    /// On the callback's thread: run the callback if this expiry belongs to
    /// the current Start(): same generation, deadline passed.
    void OnExpired(uint32_t gen)
    {
        dmq::LockGuard<dmq::RecursiveMutex> guard(lock);
        if (!armed || gen != generation || Timer::GetNow() < deadline)
            return;
        armed = false;
        // Moved out while it runs (no copy, no heap): a callback that calls
        // Start(timeout, other) replaces `callback` without destroying the
        // delegate that is running. Put back unless it was replaced.
        struct PutBack {
            OneShotState& state;
            dmq::UnicastDelegate<void()> running;
            ~PutBack() {           // also if the callback throws
                state.firing = false;
                if (state.callback.Empty())
                    state.callback = std::move(running);
            }
        } putBack{ *this, std::move(callback) };
        firing = true;
        if (putBack.running)
            putBack.running();
    }
};

} // namespace detail

class OneShotTimer
{
public:
    /// @param thread the thread the callback runs on; nullptr: the thread that
    ///        calls Timer::ProcessTimers()
    explicit OneShotTimer(dmq::IThread* thread = nullptr)
        : m_state(dmq::xmake_shared<detail::OneShotState>())
    {
        // Connected once, while the timer is stopped (Timer's rule for OnExpired)
        m_conn = m_timer.OnExpired.Connect(dmq::MakeDelegate(m_state, &detail::OneShotState::OnTick));
        SetThread(thread);
    }

    ~OneShotTimer() { Stop(); }

    OneShotTimer(const OneShotTimer&) = delete;
    OneShotTimer& operator=(const OneShotTimer&) = delete;

    /// @brief Run the callback on thread from now on (nullptr: the thread that
    ///        calls Timer::ProcessTimers()), for an owner that gets its thread
    ///        after construction. Stops the timer first.
    void SetThread(dmq::IThread* thread)
    {
        dmq::LockGuard<dmq::RecursiveMutex> guard(m_state->lock);
        StopLocked();
        m_state->thread = thread;
    }

    /// @brief Run callback once, timeout from now. If already running, the
    ///        countdown restarts and callback replaces the previous one.
    /// @param callback a synchronous delegate, e.g. MakeDelegate(this, &X::F):
    ///        it runs on the constructor's thread, so it needs no thread of its own
    void Start(dmq::Duration timeout, const dmq::Delegate<void()>& callback)
    {
        dmq::UnicastDelegate<void()> copy;
        copy = callback;
        DMQ_ASSERT_TRUE(!copy.Empty());     // an empty callback is a bug, not "keep the old one"
        dmq::LockGuard<dmq::RecursiveMutex> guard(m_state->lock);
        StartLocked(timeout);
        m_state->callback = std::move(copy);
    }

    /// @brief Start, or restart, the countdown with the callback given before.
    ///        Faults (DMQ_ASSERT) if none was.
    void Start(dmq::Duration timeout)
    {
        dmq::LockGuard<dmq::RecursiveMutex> guard(m_state->lock);
        DMQ_ASSERT_TRUE(!m_state->callback.Empty() || m_state->firing);
        StartLocked(timeout);
    }

    /// @brief Cancel: the callback won't run for the current Start(), even if
    ///        the expiry is already queued to the thread.
    void Stop()
    {
        dmq::LockGuard<dmq::RecursiveMutex> guard(m_state->lock);
        StopLocked();
    }

    /// @brief True from Start() until the callback runs or Stop().
    bool IsRunning() const
    {
        dmq::LockGuard<dmq::RecursiveMutex> guard(m_state->lock);
        return m_state->armed;
    }

private:
    // Under m_state->lock, held across the Timer call so another thread's
    // Start()/Stop() can't interleave (lock order: this, then Timer's)
    void StartLocked(dmq::Duration timeout)
    {
        // Before Timer::Start(), whose expiry is then never earlier than it
        const dmq::TimePoint deadline = Timer::GetNow() + timeout;
        // First: it rejects a zero or negative timeout (throws or faults)
        // before any state changes
        m_timer.Start(timeout, /*once=*/true);
        m_state->generation++;
        m_state->armed = true;
        m_state->deadline = deadline;
    }

    void StopLocked()
    {
        m_state->generation++;
        m_state->armed = false;
        m_timer.Stop();
    }

    std::shared_ptr<detail::OneShotState> m_state;
    Timer m_timer;
    dmq::ScopedConnection m_conn;   // declared last: disconnected before the timer goes
};

} // namespace dmq::util

#endif // DMQ_ONE_SHOT_TIMER_H
