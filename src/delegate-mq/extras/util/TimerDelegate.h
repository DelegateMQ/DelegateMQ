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

namespace detail {
/// @brief Target state shared between a TimerDelegate and its queued dispatches.
/// The TimerDelegate owns it; each queued message holds only a weak_ptr, so a
/// message still in the thread queue after the TimerDelegate is destroyed (e.g.
/// its Signal connection was dropped) finds it expired and skips the call
/// instead of touching the deleted TimerDelegate.
struct TimerTarget {
    XALLOCATOR
    dmq::UnicastDelegate<void()> fn;
};
} // namespace detail

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
/// Destroying, clearing or reassigning a TimerDelegate (including disconnecting the
/// Signal connection that owns it) cancels any tick it has queued that the target
/// thread hasn't started: the target is not invoked. Two cases are not cancelled:
///   - A tick the target thread has already started runs to completion.
///   - Signal::operator() invokes a snapshot of its connections, so a Disconnect()
///     on another thread while Timer::ProcessTimers() is emitting OnExpired can
///     still queue one tick after Disconnect() returns.
/// So disconnecting alone doesn't make it safe to destroy a raw-pointer target.
/// Before freeing the target, stop the timer, disconnect, and let the target
/// thread finish what's queued (e.g. ExitThread(), which drains the queue), as
/// NetworkNode::Stop() does.
///
/// @note Not intended for concurrent invocation. Safe when operator() is called
///       from a single ProcessTimers() context.
class TimerDelegate : public dmq::Delegate<void()>
{
    XALLOCATOR
public:
    // Constructor for pre-formed Delegate<void()> subtypes (DelegateMember, DelegateFree, etc.).
    // Stored directly as a delegate -- no std::function wrapping.
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

    // A copy shares no state with the original: its own (lazily allocated) target
    // and a fresh gate, so destroying either one cancels only its own queued ticks.
    TimerDelegate(const TimerDelegate& rhs)
        : m_thread(rhs.m_thread)
        , m_stuckTimeout(rhs.m_stuckTimeout)
    {
        m_fn = rhs.Fn();
        m_gate.OnStuck = rhs.m_gate.OnStuck;
    }

    TimerDelegate& operator=(const TimerDelegate& rhs)
    {
        if (this != &rhs)
        {
            Clear();    // cancels this delegate's queued ticks
            m_fn = rhs.Fn();
            m_thread = rhs.m_thread;
            m_stuckTimeout = rhs.m_stuckTimeout;
            m_gate.OnStuck = rhs.m_gate.OnStuck;
        }
        return *this;
    }

    // A move transfers the target and gate: ticks the source queued now belong to
    // the destination, and the source is left empty.
    TimerDelegate(TimerDelegate&& rhs) noexcept
        : m_fn(std::move(rhs.m_fn))
        , m_target(std::move(rhs.m_target))
        , m_thread(rhs.m_thread)
        , m_stuckTimeout(rhs.m_stuckTimeout)
        , m_gate(std::move(rhs.m_gate))
    {
        rhs.Clear();
    }

    TimerDelegate& operator=(TimerDelegate&& rhs) noexcept
    {
        if (this != &rhs)
        {
            m_fn = std::move(rhs.m_fn);
            m_target = std::move(rhs.m_target);    // releasing the old target cancels its ticks
            m_thread = rhs.m_thread;
            m_stuckTimeout = rhs.m_stuckTimeout;
            m_gate = std::move(rhs.m_gate);
            rhs.Clear();
        }
        return *this;
    }

    virtual void operator()() override
    {
        if (Empty())
            return;
        // Allocated on the first tick rather than at construction, so the temporary
        // that Signal::Connect() clones and discards never allocates.
        if (!m_target)
        {
            m_target = dmq::xmake_shared<detail::TimerTarget>();
            m_target->fn = std::move(m_fn);
        }
        std::weak_ptr<detail::TimerTarget> target = m_target;
        dmq::IThread& thr = *m_thread;
        m_gate.TryFire([&target, &thr](std::shared_ptr<DispatchToken> token) {
            using DelegateType = dmq::DelegateFreeAsync<void(std::weak_ptr<detail::TimerTarget>, std::shared_ptr<DispatchToken>)>;
            DelegateType(&TimerDelegate::InvokeTarget, thr)(std::move(target), std::move(token));
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
        if (Empty() || derived->Empty()) return false;
        if (!Fn().GetDelegate() || !derived->Fn().GetDelegate()) return false;
        return m_thread == derived->m_thread &&
               Fn().GetDelegate()->Equal(*derived->Fn().GetDelegate());
    }

    virtual bool operator==(std::nullptr_t) const noexcept override { return Empty(); }
    virtual bool operator!=(std::nullptr_t) const noexcept override { return !Empty(); }

    bool Empty() const noexcept { return Fn().Empty() || !m_thread; }

    /// Empties the delegate and cancels any tick it has queued that hasn't started.
    void Clear() noexcept
    {
        m_fn.Clear();
        m_target.reset();
        m_thread = nullptr;
        m_gate.Reset();
        m_gate.OnStuck.Clear();
    }
    void operator=(std::nullptr_t) noexcept { Clear(); }

private:
    // The callable: in m_fn until the first tick, then moved into m_target.
    const dmq::UnicastDelegate<void()>& Fn() const noexcept { return m_target ? m_target->fn : m_fn; }

    // Runs on the target thread. Static so the queued message never refers to the
    // TimerDelegate itself; the token is held until the call returns.
    static void InvokeTarget(std::weak_ptr<detail::TimerTarget> target, std::shared_ptr<DispatchToken> token)
    {
        (void)token;
        if (auto t = target.lock())
            t->fn();
    }

    dmq::UnicastDelegate<void()> m_fn;
    std::shared_ptr<detail::TimerTarget> m_target;
    dmq::IThread* m_thread = nullptr;
    dmq::Duration m_stuckTimeout{0};
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