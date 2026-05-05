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
/// automatically when the object is destroyed. Destruction is always clean:
/// members are destroyed in reverse declaration order, so the DataBus connection
/// disconnects before the timer is torn down.
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
#include <functional>
#include <string>

namespace dmq::databus {

template <typename T>
class DeadlineSubscription {
public:
    /// Construct a deadline-monitored DataBus subscription.
    ///
    /// @param topic     DataBus topic to subscribe to.
    /// @param deadline  Maximum allowed interval between deliveries. Must be > 0.
    /// @param handler   Called on each data delivery.
    /// @param onMissed  Called when no delivery arrives within the deadline window.
    /// @param thread    Optional worker thread for both callbacks. If nullptr,
    ///                  handler fires on the publisher's thread and onMissed fires
    ///                  on the Timer::ProcessTimers() thread.
    DeadlineSubscription(
        const std::string& topic,
        dmq::Duration deadline,
        std::function<void(const T&)> handler,
        std::function<void()> onMissed,
        dmq::IThread* thread = nullptr)
        : m_deadline(deadline)
    {
        // Connect onMissed to the timer expiry signal, dispatching to thread if provided
        if (thread) {
            m_timerConn = m_timer.OnExpired.Connect(
                dmq::util::MakeTimerDelegate(std::move(onMissed), *thread));
        } else {
            m_timerConn = m_timer.OnExpired.Connect(
                dmq::MakeDelegate(std::move(onMissed)));
        }

        // Arm the timer immediately. It fires if no delivery arrives within deadline.
        m_timer.Start(m_deadline, false);

        // Subscribe and reset the timer on every delivery
        m_conn = DataBus::Subscribe<T>(topic,
            [this, h = std::move(handler)](const T& data) {
                m_timer.Start(m_deadline, false); // reset deadline window
                h(data);
            }, thread);
    }

    ~DeadlineSubscription() = default;

    DeadlineSubscription(const DeadlineSubscription&) = delete;
    DeadlineSubscription& operator=(const DeadlineSubscription&) = delete;
    DeadlineSubscription(DeadlineSubscription&&) = delete;
    DeadlineSubscription& operator=(DeadlineSubscription&&) = delete;

private:
    // Declaration order controls destruction order (reverse).
    // m_conn disconnects first (no more timer resets via the data lambda),
    // then m_timerConn disconnects (onMissed removed from timer signal),
    // then m_timer destructs (removed from global timer list).
    dmq::Duration m_deadline;
    dmq::util::Timer m_timer;
    dmq::ScopedConnection m_timerConn;
    dmq::ScopedConnection m_conn;
};

} // namespace dmq::databus


#endif // DMQ_DEADLINE_SUBSCRIPTION_H
