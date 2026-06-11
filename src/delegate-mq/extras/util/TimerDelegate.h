#ifndef TIMER_DELEGATE_H
#define TIMER_DELEGATE_H

#include "Timer.h"
#include "../../delegate/DelegateAsync.h"
#include "../../delegate/UnicastDelegate.h"
#include <memory>

namespace dmq::util {

/// @brief Tag type whose lifetime tracks a dispatched message through the thread queue.
/// When the async closure is destroyed after Invoke() completes (or immediately on DROP),
/// the reference count falls to zero and the paired weak_ptr in PacedDispatch expires.
struct DispatchToken {
    XALLOCATOR
};

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
/// @note OnStuck fires from ProcessTimers() context -- keep it fast and non-blocking.
class PacedDispatch
{
    XALLOCATOR
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

        auto token = dmq::xmake_shared<DispatchToken>();
        m_inFlight = token;
        m_dispatchTime = Timer::GetNow();

        dispatchFn(std::move(token));
        return true;
    }

    bool IsInFlight() const { return !m_inFlight.expired(); }
    bool IsPending() const { return m_pending; }
    void Reset() { m_inFlight.reset(); m_pending = false; }

    /// Optional. Called from ProcessTimers() context -- must be fast and non-blocking.
    dmq::UnicastDelegate<void()> OnStuck;

private:
    std::weak_ptr<DispatchToken> m_inFlight;
    dmq::TimePoint m_dispatchTime{};
    bool m_pending = false;
};

/// @brief Timer-safe async delegate. Connects a periodic timer to a thread dispatch
/// with built-in queue-flood prevention.
///
/// Implements Delegate<void()> directly so it can be stored in Signal connections
/// without a DelegateFunction wrapper -- eliminating the std::function layer on
/// the hot path.
///
/// At most one message is in the target thread's queue at any time; excess timer
/// ticks are silently dropped rather than accumulated as backlog.
///
/// Prefer constructing via MakeTimerDelegate() rather than directly.
///
/// @note Not intended for concurrent invocation. Safe when operator() is called
///       from a single ProcessTimers() context.
class TimerDelegate : public dmq::Delegate<void()>
{
    XALLOCATOR
public:
    // Constructor for pre-formed Delegate<void()> subtypes (DelegateMember, DelegateFree, etc.).
    // Direct clone into m_fn — no std::function wrapping.
    TimerDelegate(const dmq::Delegate<void()>& func, dmq::IThread& thr,
                  dmq::UnicastDelegate<void()> onStuck = dmq::UnicastDelegate<void()>(),
                  dmq::Duration stuckTimeout = dmq::Duration(0))
        : m_thread(&thr)
        , m_stuckTimeout(stuckTimeout)
    {
        m_fn = func;
        m_gate.OnStuck = std::move(onStuck);
    }

    // Constructor for raw callables (lambdas, function pointers, functors).
    // SFINAE excludes Delegate<void()> subtypes so the overload above is preferred for them.
    template <typename F,
              typename = std::enable_if_t<
                  !std::is_class_v<std::decay_t<F>> ||
                  !std::is_base_of_v<dmq::Delegate<void()>, std::decay_t<F>>>>
    TimerDelegate(F&& func, dmq::IThread& thr,
                  dmq::UnicastDelegate<void()> onStuck = dmq::UnicastDelegate<void()>(),
                  dmq::Duration stuckTimeout = dmq::Duration(0))
        : m_thread(&thr)
        , m_stuckTimeout(stuckTimeout)
    {
        m_fn = dmq::DelegateFunction<void()>(std::forward<F>(func));
        m_gate.OnStuck = std::move(onStuck);
    }

    TimerDelegate(const TimerDelegate& rhs)
        : m_fn(rhs.m_fn)
        , m_thread(rhs.m_thread)
        , m_stuckTimeout(rhs.m_stuckTimeout)
    {
        m_gate.OnStuck = rhs.m_gate.OnStuck;
        // m_gate.m_inFlight and m_pending intentionally not copied: clone starts fresh
    }

    virtual void operator()() override
    {
        m_gate.TryFire([this](std::shared_ptr<DispatchToken> token) {
            using DelegateType = dmq::DelegateMemberAsync<TimerDelegate, void(std::shared_ptr<DispatchToken>)>;
            DelegateType(this, &TimerDelegate::InvokeTarget, *m_thread)(std::move(token));
        }, m_stuckTimeout);
    }

    virtual TimerDelegate* Clone() const override
    {
        return new(std::nothrow) TimerDelegate(*this);
    }

    virtual bool Equal(const dmq::DelegateBase& rhs) const override
    {
        auto derived = dynamic_cast<const TimerDelegate*>(&rhs);
        if (!derived) return false;
        if (Empty() && derived->Empty()) return true;
        if (!m_fn.GetDelegate() || !derived->m_fn.GetDelegate()) return false;
        return m_thread == derived->m_thread &&
               m_fn.GetDelegate()->Equal(*derived->m_fn.GetDelegate());
    }

    virtual bool operator==(std::nullptr_t) const noexcept override { return Empty(); }
    virtual bool operator!=(std::nullptr_t) const noexcept override { return !Empty(); }

    bool Empty() const noexcept { return m_fn.Empty() || !m_thread; }
    void Clear() noexcept { m_fn.Clear(); m_thread = nullptr; }
    void operator=(std::nullptr_t) noexcept { Clear(); }

private:
    void InvokeTarget(std::shared_ptr<DispatchToken> token)
    {
        (void)token;
        m_fn();
    }

    dmq::UnicastDelegate<void()> m_fn;
    dmq::IThread* m_thread = nullptr;
    dmq::Duration m_stuckTimeout;
    PacedDispatch m_gate;
};

/// @brief Create a timer-safe async delegate for a free function or static member function.
///
/// @code
///   void OnTick() { ... }
///   m_timerConn = m_timer.OnExpired.Connect(MakeTimerDelegate(&OnTick, m_thread));
/// @endcode
inline TimerDelegate MakeTimerDelegate(
    void(*func)(),
    dmq::IThread& thr,
    dmq::UnicastDelegate<void()> onStuck = dmq::UnicastDelegate<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    return TimerDelegate(func, thr, std::move(onStuck), stuckTimeout);
}

/// @brief Create a timer-safe async delegate for a lambda or other callable.
template <typename F>
TimerDelegate MakeTimerDelegate(
    F&& func,
    dmq::IThread& thr,
    dmq::UnicastDelegate<void()> onStuck = dmq::UnicastDelegate<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    return TimerDelegate(std::forward<F>(func), thr, std::move(onStuck), stuckTimeout);
}

/// @brief Create a timer-safe async delegate for a non-const member function.
///
/// At most one dispatch is in flight; excess timer ticks are dropped.
/// DelegateAsyncWait is structurally prevented -- dispatch is always non-blocking.
///
/// @param obj            Raw pointer to target object. Must outlive the delegate.
/// @param method         void() non-const member function to invoke.
/// @param thr            Thread on which method is dispatched.
/// @param onStuck        Optional. Called from ProcessTimers() when stuck is detected.
/// @param stuckTimeout   Duration after which a non-expiring in-flight token triggers
///                       onStuck. Zero disables.
///
/// @code
///   m_timerConn = m_timer.OnExpired.Connect(
///       MakeTimerDelegate(this, &MyClass::OnTick, m_thread));
///
///   // With stuck detection:
///   m_timerConn = m_timer.OnExpired.Connect(
///       MakeTimerDelegate(this, &MyClass::OnTick, m_thread,
///           dmq::MakeDelegate(this, &MyClass::OnThreadStuck), std::chrono::seconds(2)));
/// @endcode
template <typename TClass>
TimerDelegate MakeTimerDelegate(
    TClass* obj,
    void (TClass::*method)(),
    dmq::IThread& thr,
    dmq::UnicastDelegate<void()> onStuck = dmq::UnicastDelegate<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    return TimerDelegate(dmq::MakeDelegate(obj, method), thr, std::move(onStuck), stuckTimeout);
}

/// @brief Create a timer-safe async delegate for a const member function.
template <typename TClass>
TimerDelegate MakeTimerDelegate(
    const TClass* obj,
    void (TClass::*method)() const,
    dmq::IThread& thr,
    dmq::UnicastDelegate<void()> onStuck = dmq::UnicastDelegate<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    return TimerDelegate(dmq::MakeDelegate(obj, method), thr, std::move(onStuck), stuckTimeout);
}

/// @brief Create a timer-safe async delegate for a non-const member function (shared_ptr owner).
///
/// Captures a weak_ptr internally via DelegateMemberSp. If the object has been destroyed
/// by the time the dispatch fires, the invocation is silently skipped.
template <typename TClass>
TimerDelegate MakeTimerDelegate(
    std::shared_ptr<TClass> obj,
    void (TClass::*method)(),
    dmq::IThread& thr,
    dmq::UnicastDelegate<void()> onStuck = dmq::UnicastDelegate<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    return TimerDelegate(dmq::MakeDelegate(obj, method), thr, std::move(onStuck), stuckTimeout);
}

/// @brief Create a timer-safe async delegate for a const member function (shared_ptr owner).
template <typename TClass>
TimerDelegate MakeTimerDelegate(
    std::shared_ptr<TClass> obj,
    void (TClass::*method)() const,
    dmq::IThread& thr,
    dmq::UnicastDelegate<void()> onStuck = dmq::UnicastDelegate<void()>(),
    dmq::Duration stuckTimeout = dmq::Duration(0))
{
    return TimerDelegate(dmq::MakeDelegate(obj, method), thr, std::move(onStuck), stuckTimeout);
}

} // namespace dmq::util

#endif