#ifndef TIMER_DELEGATE_H
#define TIMER_DELEGATE_H

#include "Timer.h"
#include "../../delegate/DelegateAsync.h"
#include <memory>
#include <functional>

namespace dmq::util {

/// @brief Tag type whose lifetime tracks a dispatched message through the thread queue.
/// When the async closure is destroyed after Invoke() completes (or immediately on DROP),
/// the reference count falls to zero and the paired weak_ptr in PacedDispatch expires.
struct DispatchToken {};

/// @brief Gate that enforces at-most-one-in-flight dispatch for a periodic timer source.
///
/// Solves two problems when a periodic timer drives an async dispatch:
///   - ProcessTimers() stalling: skips DispatchDelegate() entirely if previous message
///     is still queued, so ProcessTimers() never blocks on a TIMEOUT-policy thread.
///   - Queue flooding: at most one pending callback exists at any time; a slow thread
///     won't accumulate a backlog of stale timer events.
///
/// The caller captures the shared_ptr<DispatchToken> inside the async lambda. When
/// Invoke() completes and the DelegateMsg is destroyed, the token's ref count drops to
/// zero, the weak_ptr expires, and the next TryFire() can proceed.
///
/// If the message is dropped (FullPolicy::DROP), the DelegateMsg destructs immediately,
/// the token dies, and TryFire() retries on the next timer expiry.
///
/// @note Not thread-safe for concurrent TryFire() calls. Safe when ProcessTimers() runs
///       on a single thread, which is the standard usage.
/// @note OnStuck fires from ProcessTimers() context — keep it fast and non-blocking.
class PacedDispatch
{
public:
    /// @brief Attempt a dispatch if no previous one is in flight.
    ///
    /// @param dispatchFn  Callable receiving a shared_ptr<DispatchToken> by value.
    ///                    Must capture the token in the async closure so its lifetime
    ///                    tracks the message through the queue.
    /// @param stuckTimeout If non-zero and the token is still live after this duration,
    ///                     OnStuck is called. Default: disabled.
    /// @return true if dispatch was attempted; false if skipped (already in flight).
    template <typename F>
    bool TryFire(F&& dispatchFn, dmq::Duration stuckTimeout = dmq::Duration(0))
    {
        if (!m_inFlight.expired())
        {
            if (stuckTimeout.count() > 0 && OnStuck)
            {
                if (Timer::GetNow() - m_dispatchTime > stuckTimeout)
                    OnStuck();
            }
            m_pending = true;
            return false;
        }

        m_pending = false;

        auto token = std::make_shared<DispatchToken>();
        m_inFlight = token;
        m_dispatchTime = Timer::GetNow();

        dispatchFn(std::move(token));
        return true;
    }

    bool IsInFlight() const { return !m_inFlight.expired(); }
    bool IsPending() const { return m_pending; }
    void Reset() { m_inFlight.reset(); m_pending = false; }

    /// Optional. Called from ProcessTimers() context — must be fast and non-blocking.
    std::function<void()> OnStuck;

private:
    std::weak_ptr<DispatchToken> m_inFlight;
    dmq::TimePoint m_dispatchTime{};
    bool m_pending = false;
};

/// @brief Callable returned by MakeTimerDelegate.
///
/// Connects a periodic timer to an async thread dispatch with built-in queue-flood
/// prevention. At most one message is in the target thread's queue at any time.
/// If the target thread is busy, the timer tick is silently skipped rather than
/// accumulated as backlog.
///
/// Prefer constructing via MakeTimerDelegate() rather than directly.
///
/// @note TimerDelegate is a stateful functor-wrapper rather than a Delegate<> 
/// subtype to avoid boilerplate (Clone, Equal, etc.) while carrying PacedDispatch state.
///
/// @note Not intended for concurrent invocation. Safe when operator() is called
///       from a single ProcessTimers() context.
class TimerDelegate
{
public:
    template <typename F>
    TimerDelegate(F&& func, dmq::IThread& thr,
                  std::function<void()> onStuck = std::function<void()>(),
                  dmq::Duration stuckTimeout = dmq::Duration(0))
        : m_fn(std::forward<F>(func))
        , m_thread(&thr)
        , m_stuckTimeout(stuckTimeout)
    {
        m_gate.OnStuck = std::move(onStuck);
    }

    void operator()() const
    {
        m_gate.TryFire([this](std::shared_ptr<DispatchToken> token) {
            dmq::MakeDelegate([fn = m_fn, tok = std::move(token)]() {
                fn();
            }, *m_thread)();
        }, m_stuckTimeout);
    }

private:
    std::function<void()> m_fn;
    dmq::IThread* m_thread;
    dmq::Duration m_stuckTimeout;
    mutable PacedDispatch m_gate;
};

/// @brief Create a timer-safe async delegate for a free function or static member function.
///
/// Accepts any void() function pointer, including static class members.
///
/// @code
///   void OnTick() { ... }
///   m_timerConn = m_timer.OnExpired.Connect(
///       MakeTimerDelegate(&OnTick, m_thread));
/// @endcode
inline auto MakeTimerDelegate(
    void(*func)(),
    dmq::IThread& thr,
    std::function<void()> onStuck = std::function<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    return dmq::MakeDelegate(TimerDelegate(func, thr, std::move(onStuck), stuckTimeout));
}

/// @brief Create a timer-safe async delegate for a lambda or other callable.
template <typename F>
auto MakeTimerDelegate(
    F&& func,
    dmq::IThread& thr,
    std::function<void()> onStuck = std::function<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    return dmq::MakeDelegate(TimerDelegate(std::forward<F>(func), thr, std::move(onStuck), stuckTimeout));
}

/// @brief Create a timer-safe async delegate for a non-const member function.
///
/// Returns a callable that can be connected directly to Timer::OnExpired.
/// At most one dispatch is in flight at any time; excess timer ticks are dropped
/// rather than queued. DelegateAsyncWait is structurally prevented — the dispatch
/// is always non-blocking.
///
/// @param obj            Raw pointer to the target object. Must outlive the delegate.
/// @param method         void() non-const member function to invoke.
/// @param thr            Thread on which method is dispatched. Must use FullPolicy::FAULT
///                       or FullPolicy::DROP, not TIMEOUT (see Timer.h).
/// @param onStuck        Optional. Called from ProcessTimers() when stuck is detected.
/// @param stuckTimeout   Duration after which a non-expiring in-flight token triggers
///                       onStuck. Zero disables stuck detection.
///
/// @code
///   m_timerConn = m_timer.OnExpired.Connect(
///       MakeTimerDelegate(this, &MyClass::OnTick, m_thread));
///
///   // With stuck detection:
///   m_timerConn = m_timer.OnExpired.Connect(
///       MakeTimerDelegate(this, &MyClass::OnTick, m_thread,
///           [this]{ OnThreadStuck(); }, std::chrono::seconds(2)));
/// @endcode
template <typename TClass>
auto MakeTimerDelegate(
    TClass* obj,
    void (TClass::*method)(),
    dmq::IThread& thr,
    std::function<void()> onStuck = std::function<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    return dmq::MakeDelegate(TimerDelegate(
        [obj, method]() { (obj->*method)(); },
        thr, std::move(onStuck), stuckTimeout));
}

/// @brief Create a timer-safe async delegate for a const member function.
template <typename TClass>
auto MakeTimerDelegate(
    const TClass* obj,
    void (TClass::*method)() const,
    dmq::IThread& thr,
    std::function<void()> onStuck = std::function<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    return dmq::MakeDelegate(TimerDelegate(
        [obj, method]() { (obj->*method)(); },
        thr, std::move(onStuck), stuckTimeout));
}

/// @brief Create a timer-safe async delegate for a non-const member function (shared_ptr owner).
///
/// Captures a weak_ptr to the object. If the object has been destroyed by the time
/// the dispatch fires, the invocation is silently skipped.
template <typename TClass>
auto MakeTimerDelegate(
    std::shared_ptr<TClass> obj,
    void (TClass::*method)(),
    dmq::IThread& thr,
    std::function<void()> onStuck = std::function<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    std::weak_ptr<TClass> weak = obj;
    return dmq::MakeDelegate(TimerDelegate(
        [weak, method]() {
            if (auto p = weak.lock())
                (p.get()->*method)();
        },
        thr, std::move(onStuck), stuckTimeout));
}

/// @brief Create a timer-safe async delegate for a const member function (shared_ptr owner).
template <typename TClass>
auto MakeTimerDelegate(
    std::shared_ptr<TClass> obj,
    void (TClass::*method)() const,
    dmq::IThread& thr,
    std::function<void()> onStuck = std::function<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    std::weak_ptr<TClass> weak = obj;
    return dmq::MakeDelegate(TimerDelegate(
        [weak, method]() {
            if (auto p = weak.lock())
                (p.get()->*method)();
        },
        thr, std::move(onStuck), stuckTimeout));
}

} // namespace dmq::util

#endif
