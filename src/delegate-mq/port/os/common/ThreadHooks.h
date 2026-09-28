#ifndef _THREAD_HOOKS_H
#define _THREAD_HOOKS_H

/// @file ThreadHooks.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Start, exit and idle handler support shared by the dmq::os::Thread ports.
///
/// @details Each port exposes SetStartHandler()/SetExitHandler()/SetIdleHandler()
/// and calls these helpers from its worker loop, so the handler semantics stay the
/// same on every port:
/// * The start handler runs on the worker thread before any message, and
///   CreateThread() returns only after it completes.
/// * The exit handler runs on the worker thread after the last message, on every
///   loop return path (ThreadExitGuard).
/// * The idle handler runs on the worker thread once its queue has been empty for
///   the idle interval, then every interval while it stays empty; any processed
///   message restarts the countdown (ThreadIdleTimer).
///
/// Both helpers take a copy of the handler when the loop starts, so they can run
/// after the owning Thread object has been destroyed (a thread that exits itself).

#include "delegate/DelegateOpt.h"
#include "delegate/UnicastDelegate.h"

namespace dmq::os {

/// @brief Runs a thread's exit handler when the worker loop's stack frame unwinds.
/// Declare it at the top of the loop function, before any return.
class ThreadExitGuard
{
public:
    explicit ThreadExitGuard(const dmq::UnicastDelegate<void()>& handler) : m_handler(handler) {}
    ~ThreadExitGuard() { Fire(); }

    /// @brief Run the exit handler now rather than at scope exit, e.g. before the
    /// loop signals ExitThread() that it has finished. Runs it at most once.
    void Fire()
    {
        if (!m_fired && m_handler)
            m_handler();
        m_fired = true;
    }

private:
    ThreadExitGuard(const ThreadExitGuard&) = delete;
    ThreadExitGuard& operator=(const ThreadExitGuard&) = delete;

    dmq::UnicastDelegate<void()> m_handler;
    bool m_fired = false;
};

/// @brief Tracks when a worker loop's idle handler is next due.
/// @details Usage per loop iteration: bound the queue wait with WaitTime(); if the
/// wait ends with the queue empty and IsDue(), call Run(); after processing a
/// message, call Restart(). All methods are no-ops when no handler is set.
class ThreadIdleTimer
{
public:
    ThreadIdleTimer(const dmq::UnicastDelegate<void()>& handler, dmq::Duration interval)
        : m_handler(handler), m_interval(interval), m_due(dmq::Clock::now() + interval) {}

    /// @return true if an idle handler is set.
    bool Enabled() const { return static_cast<bool>(m_handler); }

    /// @brief Bound a queue wait so the loop wakes when the idle handler is due.
    /// @param[in] wait - the loop's own wait bound (e.g. the watchdog heartbeat),
    ///   or a negative duration to mean "wait forever".
    /// @return the shorter of `wait` and the time until the idle handler is due
    ///   (never negative when the idle handler is enabled); `wait` unchanged otherwise.
    dmq::Duration WaitTime(dmq::Duration wait) const
    {
        if (!Enabled())
            return wait;
        dmq::Duration untilIdle = m_due - dmq::Clock::now();
        if (untilIdle < dmq::Duration::zero())
            untilIdle = dmq::Duration::zero();
        if (wait < dmq::Duration::zero() || untilIdle < wait)
            return untilIdle;
        return wait;
    }

    /// @return true if the idle handler is set and its interval has elapsed.
    bool IsDue() const { return Enabled() && dmq::Clock::now() >= m_due; }

    /// @brief Restart the countdown. Call after processing each message.
    void Restart()
    {
        if (Enabled())
            m_due = dmq::Clock::now() + m_interval;
    }

    /// @brief Invoke the idle handler and restart the countdown.
    void Run()
    {
        m_handler();
        Restart();
    }

private:
    const dmq::UnicastDelegate<void()> m_handler;
    const dmq::Duration m_interval;
    dmq::TimePoint m_due;
};

} // namespace dmq::os

#endif
