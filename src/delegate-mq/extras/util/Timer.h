#ifndef _TIMER_H
#define _TIMER_H

#include "../../delegate/DelegateOpt.h"
#include "../../delegate/Signal.h"
#include <atomic>
#include <list>
#include <new>

namespace dmq::util {

/// @brief A thread-safe timer class that provides periodic or one-shot callbacks.
///
/// @details
/// The Timer class allows clients to register for callbacks (`OnExpired`) that are invoked
/// when a specified timeout interval elapses.
///
/// **Key Features:**
/// * **Thread Safe:** All public API methods (`Start`, `Stop`, etc.) are thread-safe and can be called
///   from any thread.
/// * **Flexible Modes:** Supports both one-shot (`once = true`) and periodic (`once = false`) operation.
/// * **Deterministic Execution:** Callbacks are invoked on the thread that calls `ProcessTimers()`.
///   This allows the user to control exactly which thread executes the timer logic (e.g., Main Thread,
///   GUI Thread, or a dedicated Worker Thread).
///
/// **Usage — Preferred Pattern:**
/// Call `ProcessTimers()` from the highest-priority context that can preempt all watched threads.
/// On an RTOS port (FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2, NuttX) that is the highest-priority
/// task, never an ISR: `ProcessTimers()` takes OS mutexes to copy each expired timer's
/// subscribers. Only bare metal (`DMQ_THREAD_NONE`, no OS mutexes) can call it from a hardware
/// timer ISR. Connect `OnExpired` to one of:
///   - A plain (synchronous) delegate for inline, fast callbacks (no blocking, no allocation).
///   - An async delegate (`MakeDelegate(..., thread)`) to dispatch work to a worker thread.
///     The target thread MUST use `FullPolicy::FAULT` or `FullPolicy::DROP` — never `TIMEOUT`.
///     (`FAULT` crashes immediately if the queue is full, making overflow visible during testing;
///      `DROP` silently skips the dispatch, which is acceptable for periodic heartbeats.)
///
/// **ProcessTimers() Blocking Invariant:**
/// `ProcessTimers()` MUST NEVER block. Violating this starves the timer subsystem and defeats
/// the watchdog. Three known causes:
///
///   **Cause 1 — Async dispatch to a TIMEOUT-policy thread:**
///   `MakeDelegate(fn, thread)()` inside a timer callback calls `DispatchDelegate()` on the
///   target thread. If that thread's queue is full and its policy is `FullPolicy::TIMEOUT`,
///   `ProcessTimers()` stalls for up to `dispatchTimeout` (default 2 s). Fix: target threads
///   that receive timer dispatches must use `FAULT` or `DROP`, never `TIMEOUT`.
///
///   **Cause 2 — DelegateAsyncWait connected to OnExpired:**
///   `DelegateAsyncWait` blocks its caller until the target thread returns the result.
///   If one is connected to `OnExpired`, `ProcessTimers()` blocks indefinitely waiting
///   for the remote call to complete. Fix: never connect `DelegateAsyncWait` to a timer signal.
///   Only plain async delegates (`MakeDelegate(..., thread)`) are safe for timer callbacks.
///
///   **Cause 3 — Slow synchronous callback:**
///   A plain (non-async) delegate connected to `OnExpired` runs inline inside `ProcessTimers()`.
///   If that function acquires a lock, does I/O, or otherwise takes significant time,
///   `ProcessTimers()` is delayed for that duration. Fix: keep synchronous timer callbacks fast;
///   dispatch any slow work asynchronously to a worker thread instead.
///
/// @see SafeTimer.cpp for examples on how to handle callbacks safely with object lifetimes.
class Timer
{
    XALLOCATOR
public:
    /// Clients connect to OnExpired to get timer expiration callbacks.
    dmq::Signal<void(void)> OnExpired;

    /// Constructor
    Timer(void);

    /// Destructor
    ~Timer(void);

    /// Starts a timer for callbacks on the specified timeout interval.
    /// @param[in] timeout - the timeout.
    /// @param[in] once - true if only one timer expiration
    void Start(dmq::Duration timeout, bool once = false);

    /// Stops a timer.
    void Stop();

    /// Gets the enabled state of a timer.
    /// @return TRUE if the timer is enabled, FALSE otherwise.
    bool Enabled() { return m_enabled; }

    /// Get the time. 
    /// @return The time now. 
    static dmq::TimePoint GetNow();

    /// Called on a periodic basic to service all timer instances. 
    /// @TODO: Call periodically for timer expiration handling.
    static void ProcessTimers();

private:
    // Prevent inadvertent copying of this object
    Timer(const Timer&);
    Timer& operator=(const Timer&);

    /// Called to check for expired timers and callback registered clients.
    bool CheckExpired();

    /// Get list head using the "Immortal" Pattern
    static Timer*& GetTimersHead()
    {
        // Static head pointer. NEVER delete. Prevents lock from being destroyed 
        // before the last Timer destructor runs at app shutdown.
        static Timer* head = nullptr;
        return head;
    }

    /// Get lock using the "Immortal" Pattern. Guards the timer list and each
    /// timer's state; held only for short, bounded list work.
    /// @note dmq::CriticalSection, not dmq::RecursiveMutex, so the list scan in
    /// ProcessTimers() never blocks on an OS mutex and bare metal can run it from
    /// a hardware ISR. Nothing that can block or allocate may run under it
    /// (see GetProcessLock()).
    static dmq::CriticalSection& GetLock()
    {
        // Allocate on heap and NEVER delete. Prevents lock from being destroyed
        // before the last Timer destructor runs at app shutdown.
        static dmq::CriticalSection* lock = new(std::nothrow) dmq::CriticalSection();
        if (!lock)
            BAD_ALLOC();
        return *lock;
    }

    /// Held by ProcessTimers() from finding the expired timers until their
    /// subscribers are copied, and by ~Timer() before it unlinks itself, so a
    /// timer can't be destroyed in between. Copying the subscribers takes the
    /// signal's OS mutexes and can allocate, so it can't happen under GetLock().
    /// Lock order: this, then GetLock(). Not held while callbacks run.
    static dmq::RecursiveMutex& GetProcessLock()
    {
        // Immortal, like GetLock().
        static dmq::RecursiveMutex* lock = new(std::nothrow) dmq::RecursiveMutex();
        if (!lock)
            BAD_ALLOC();
        return *lock;
    }

    dmq::Duration m_timeout = dmq::Duration(0);		
    dmq::TimePoint m_expireTime;
    std::atomic<bool> m_enabled{false};
    bool m_once = false;
    Timer* m_next = nullptr;
    static std::atomic<bool> m_timerStopped;
};

} // namespace dmq::util

#endif
