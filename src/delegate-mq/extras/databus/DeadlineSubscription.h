#ifndef DMQ_DEADLINE_SUBSCRIPTION_H
#define DMQ_DEADLINE_SUBSCRIPTION_H

/// @file DeadlineSubscription.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief RAII helper that combines a DataBus subscription with a deadline timer.
///
/// @details
/// `DeadlineSubscription<T>` monitors a DataBus topic and fires a user callback
/// if no message arrives within a configurable deadline window. It is built
/// entirely from existing DelegateMQ primitives — `DataBus::Subscribe`, `Timer`,
/// and `ScopedConnection` — and adds no library-internal mechanism.
///
/// **How it works:**
/// A `Timer` is started at construction time. Every incoming message resets the
/// timer. If the timer fires before the next message arrives, the `onMissed`
/// callback is invoked. Both the data handler and the deadline callback are
/// dispatched to the same optional worker thread.
///
/// **Lifetime:**
/// The object is non-copyable and non-movable. All resources — the DataBus
/// connection, the timer expiry connection, and the timer itself — are released
/// automatically when the object is destroyed. Members are destroyed in reverse
/// declaration order, so the DataBus connection disconnects before the timer is
/// torn down.
///
/// When a `thread` argument is supplied, stop or join that thread before
/// destroying this object. `ScopedConnection` prevents new callbacks from being
/// queued, but any callback already waiting in the thread's message queue will
/// still execute and will access members of this object. This is the same rule
/// that applies to any async delegate that captures a raw pointer.
///
/// **`Timer::ProcessTimers()` requirement:**
/// The deadline timer fires only when `Timer::ProcessTimers()` is called. On
/// platforms with a running `Thread`, this is typically driven by the thread's
/// internal timer. On bare-metal targets, call `ProcessTimers()` from the main
/// super-loop or a SysTick handler. If `ProcessTimers()` is not called, the
/// deadline callback silently never fires.
///
/// **`onMissed` callback context:**
/// - With a `thread` argument: the callback is dispatched asynchronously to
///   that thread, matching the delivery context of the data handler.
/// - Without a `thread` argument: the callback fires synchronously on whatever
///   thread calls `Timer::ProcessTimers()`. On bare-metal this may be an ISR —
///   keep the callback short and non-blocking.
///
/// **Usage:**
/// @code
/// dmq::DeadlineSubscription<SensorData> m_watch{
///     "sensor/temp",
///     std::chrono::milliseconds(500),
///     [](const SensorData& d) { /* handle data */ },
///     []()                    { /* deadline missed — sensor silent */ },
///     &m_workerThread
/// };
/// @endcode

#include "DataBus.h"
#include "extras/util/Timer.h"
#include "delegate/UnicastDelegate.h"
#include <string>

namespace dmq::databus {

template <typename T>
class DeadlineSubscription {
    XALLOCATOR
public:
    /// Construct a deadline-monitored DataBus subscription.
    ///
    /// @param topic     DataBus topic to subscribe to.
    /// @param deadline  Maximum allowed interval between deliveries. Must be > 0.
    /// @param handler   Called on each data delivery. Accepts any callable (lambda,
    ///                  function pointer, member delegate, UnicastDelegate).
    /// @param onMissed  Called when no delivery arrives within the deadline window.
    ///                  Accepts any callable with signature void().
    /// @param thread    Optional worker thread for both callbacks. If nullptr,
    ///                  handler fires on the publisher's thread and onMissed fires
    ///                  on the Timer::ProcessTimers() thread.
    template <typename H, typename M>
    DeadlineSubscription(
        const dmq::xstring& topic,
        dmq::Duration deadline,
        H&& handler,
        M&& onMissed,
        dmq::IThread* thread = nullptr)
        : m_deadline(deadline)
    {
        if constexpr (std::is_base_of_v<dmq::Delegate<void(const T&)>, std::decay_t<H>>)
            m_handler = std::forward<H>(handler);
        else
            m_handler = dmq::DelegateFunction<void(const T&)>(std::forward<H>(handler));

        if constexpr (std::is_base_of_v<dmq::Delegate<void()>, std::decay_t<M>>)
            m_onMissed = std::forward<M>(onMissed);
        else
            m_onMissed = dmq::DelegateFunction<void()>(std::forward<M>(onMissed));

        // Connect onMissed to the timer expiry signal, dispatching to thread if provided
        if (thread) {
            m_timerConn = m_timer.OnExpired.Connect(
                dmq::util::MakeTimerDelegate(this, &DeadlineSubscription::OnTimerExpired, *thread));
        } else {
            m_timerConn = m_timer.OnExpired.Connect(
                dmq::MakeDelegate(this, &DeadlineSubscription::OnTimerExpired));
        }

        // Arm the timer immediately. It fires if no delivery arrives within deadline.
        m_timer.Start(m_deadline, false);

        // Subscribe and reset the timer on every delivery
        m_conn = DataBus::Subscribe<T>(topic, dmq::MakeDelegate(this, &DeadlineSubscription::OnDataReceived), thread);
    }

    ~DeadlineSubscription() = default;

    DeadlineSubscription(const DeadlineSubscription&) = delete;
    DeadlineSubscription& operator=(const DeadlineSubscription&) = delete;
    DeadlineSubscription(DeadlineSubscription&&) = delete;
    DeadlineSubscription& operator=(DeadlineSubscription&&) = delete;

private:
    void OnDataReceived(const T& data) {
        m_timer.Start(m_deadline, false); // reset deadline window
        m_handler(data);
    }

    void OnTimerExpired() {
        m_onMissed();
    }

    // Declaration order controls destruction order (reverse).
    // m_conn disconnects first (no more timer resets via the data lambda),
    // then m_timerConn disconnects (onMissed removed from timer signal),
    // then m_timer destructs (removed from global timer list).
    dmq::Duration m_deadline;
    dmq::UnicastDelegate<void(const T&)> m_handler;
    dmq::UnicastDelegate<void()> m_onMissed;
    dmq::util::Timer m_timer;
    dmq::ScopedConnection m_timerConn;
    dmq::ScopedConnection m_conn;
};

} // namespace dmq::databus


#endif // DMQ_DEADLINE_SUBSCRIPTION_H
