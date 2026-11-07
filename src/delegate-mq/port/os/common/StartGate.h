#ifndef _DMQ_START_GATE_H
#define _DMQ_START_GATE_H

/// @file
/// @brief Optional start gate: hold any number of threads until one "go".
///
/// @details A `dmq::os::Thread` given a gate with `SetStartGate()` is created
/// as usual: `CreateThread()` runs its start handler and returns once the
/// thread is ready, and other threads can dispatch to it. It then waits at the
/// gate and processes nothing, at any priority, until `Open()` is called. Every
/// thread holding at the gate is released at once.
///
/// Typical startup: create every thread with the same gate, wire up
/// subscriptions and connections, then call `Open()`. No thread runs a
/// message while others are still being created or connected.
///
/// @code
/// dmq::os::StartGate gate;
/// sensors.SetStartGate(&gate);    // before CreateThread()
/// network.SetStartGate(&gate);
/// sensors.CreateThread();         // ready, holding at the gate
/// network.CreateThread();
/// // ... subscribe, connect; dispatched messages wait in the queues ...
/// gate.Open();                    // all gated threads start together
/// @endcode
///
/// - A thread with no gate (the default) is unaffected.
/// - The start handler runs before the gate, during `CreateThread()`. Keep it
///   to local setup: other threads may not exist yet.
/// - `Open()` is permanent: a thread created (or re-created) after it passes
///   straight through. `Close()` makes the gate hold threads again.
/// - There is no timeout. A gate that is never opened holds its threads
///   until they are told to exit.
/// - `ExitThread()` on a held thread releases it without running any queued
///   message; those messages are canceled as for `ExitPolicy::DISCARD`.
/// - A held thread is not reported by the watchdog. Its watchdog starts when
///   the gate opens.
/// - Lifetime: the gate may be destroyed right after `Open()`, e.g. a local in
///   a startup function. Its destructor waits for every released thread to
///   leave the gate (they may not have run yet). Destroying it while a thread
///   is still held (not opened, not exited) faults. `CreateThread()` uses the
///   gate again, so call `SetStartGate(nullptr)` before re-creating a thread
///   whose gate is gone. On an RTOS, don't destroy it before the scheduler runs.
///
/// Supported where `DMQ_HAS_START_GATE` is defined (delegate/DelegateSelect.h):
/// every thread port (not bare metal, which has no threads).

#include "delegate/DelegateOpt.h"
#include "delegate/Semaphore.h"
#include <atomic>
#include <cstdint>
#include <optional>

#if defined(DMQ_HAS_START_GATE) && defined(DMQ_HAS_SEMAPHORE)

namespace dmq::os {

class StartGateClient;

class StartGate
{
public:
    StartGate() = default;

    /// Waits for every released thread to leave the gate. Faults if a thread
    /// is still held: it would wait forever.
    ~StartGate()
    {
        for (;;)
        {
            {
                dmq::LockGuard<dmq::Mutex> lock(m_mutex);
                DMQ_ASSERT_TRUE(m_held == 0);   // a thread still held would never leave
                if (m_users == 0)
                    return;
                m_destroying = true;
            }
            // Signaled under m_mutex by the last thread to leave; relocking
            // above means that signal has completed before members go away
            m_idle.Wait(dmq::Duration::max());
        }
    }

    /// Release every thread created with this gate so far, whether it is
    /// already waiting or still on its way to the gate. Threads created later
    /// pass straight through until Close().
    void Open()
    {
        dmq::LockGuard<dmq::Mutex> lock(m_mutex);
        m_open = true;
        m_openCount++;
        m_held = 0;
        for (Waiter* w = m_waiters; w != nullptr; w = w->next)
            w->sem->Signal();
    }

    /// Hold threads created from now on (to reuse the gate for another
    /// startup). Threads released by an earlier Open() still start, even if
    /// they haven't reached the gate or woken yet.
    void Close()
    {
        dmq::LockGuard<dmq::Mutex> lock(m_mutex);
        m_open = false;
    }

    bool IsOpen() const
    {
        dmq::LockGuard<dmq::Mutex> lock(m_mutex);
        return m_open;
    }

    StartGate(const StartGate&) = delete;
    StartGate& operator=(const StartGate&) = delete;

private:
    friend class StartGateClient;

    /// What a thread got from Attach(): the open count then, and whether the
    /// gate was holding (it counts in m_held until released or detached).
    struct Ticket
    {
        uint32_t openCount = 0;
        bool held = false;
    };

    /// One per waiting thread, on that thread's stack; `sem` is the thread's
    /// own (StartGateClient), so the gate allocates nothing.
    struct Waiter
    {
        dmq::Semaphore* sem = nullptr;
        Waiter* next = nullptr;
    };

    /// CreateThread(), before the thread starts: the gate is in use until the
    /// thread Detach()es, so it can't be destroyed under it.
    Ticket Attach()
    {
        dmq::LockGuard<dmq::Mutex> lock(m_mutex);
        m_users++;
        Ticket t;
        t.openCount = m_openCount;
        t.held = !m_open;
        if (t.held)
            m_held++;
        return t;
    }

    /// The thread is done with the gate (left Wait(), or was never started).
    /// Last access to the gate by that thread.
    void Detach(const Ticket& t)
    {
        dmq::LockGuard<dmq::Mutex> lock(m_mutex);
        if (t.held && t.openCount == m_openCount)
            m_held--;     // not released by an Open(), so still counted
        m_users--;
        if (m_users == 0 && m_destroying)
            m_idle.Signal();
    }

    /// Attached while open (passes through even if Close() came since), open
    /// now, or opened since the ticket was taken.
    bool Released(const Ticket& t) const
    {
        return !t.held || m_open || m_openCount != t.openCount;
    }

    /// On the thread itself, after its start handshake and before its message
    /// loop. Blocks until the gate opens (now or since Attach()) or `exit` is
    /// set and `sem` signaled (StartGateClient::Wake()). Detaches before
    /// returning, so the gate isn't touched afterwards.
    /// @return true if the gate opened: the thread enters its loop, and an exit
    /// requested meanwhile follows its ExitPolicy (DRAIN runs the queued
    /// messages). false if the thread was told to exit while still held: it
    /// runs nothing.
    bool Wait(const std::atomic<bool>& exit, const Ticket& t, dmq::Semaphore& sem)
    {
        Waiter self;
        self.sem = &sem;
        bool opened;
        bool waiting = false;   // decided once, under the lock: on the list or not
        {
            dmq::LockGuard<dmq::Mutex> lock(m_mutex);
            opened = Released(t);
            if (!opened && !exit.load())
            {
                self.next = m_waiters;
                m_waiters = &self;
                waiting = true;
            }
        }

        // Open() signals under m_mutex; ExitThread() signals `sem` directly
        // after setting `exit`. A signal sent before this wait starts is kept
        // by the semaphore, and a stale one only causes an extra check.
        while (waiting)
        {
            sem.Wait(dmq::Duration::max());
            dmq::LockGuard<dmq::Mutex> lock(m_mutex);
            if (exit.load() || Released(t))
            {
                opened = Released(t);
                Unlink(&self);
                waiting = false;
            }
        }

        Detach(t);
        return opened;
    }

    void Unlink(Waiter* target)
    {
        for (Waiter** pp = &m_waiters; *pp != nullptr; pp = &(*pp)->next)
        {
            if (*pp == target)
            {
                *pp = target->next;
                return;
            }
        }
    }

    mutable dmq::Mutex m_mutex;
    bool m_open = false;
    uint32_t m_openCount = 0;   // Open() calls; wraps harmlessly (only compared for change)
    uint32_t m_users = 0;       // threads attached and not yet detached
    uint32_t m_held = 0;        // of those, attached while closed and not released since
    bool m_destroying = false;
    Waiter* m_waiters = nullptr;
    dmq::Semaphore m_idle;      // ~StartGate() waits on it for m_users to reach 0
};

/// A thread port's start gate state: one member per `dmq::os::Thread`, so
/// every port wires the gate the same way.
///
/// Port calls: `Set()` from SetStartGate(); `OnCreate()` in CreateThread()
/// before the thread exists, and `OnCreateFailed()` if it then can't be
/// created; `Enabled()`/`Wait()`/`Passed()` on the thread before its loop;
/// `Wake()` in ExitThread() after setting the exit flag, and `Leave()` on its
/// self-exit path; `IsHeld()` in WatchdogCheck().
class StartGateClient
{
public:
    void Set(StartGate* gate) { m_gate = gate; }
    StartGate* Get() const { return m_gate; }

    void OnCreate()
    {
        m_held.store(m_gate != nullptr);
        m_attached = false;
        if (m_gate)
        {
            if (!m_wake)
                m_wake.emplace();   // created here, not in the Thread constructor (RTOS heap)
            m_ticket = m_gate->Attach();
            m_attached = true;
        }
    }

    void OnCreateFailed() { Leave(); }

    /// ExitThread() on a self-exit, from the thread itself: if it exited from
    /// its start handler it never reaches Wait(), so detach here, while the
    /// object is alive. No effect once the thread has passed the gate.
    void Leave()
    {
        if (m_attached)
            m_gate->Detach(m_ticket);
        m_attached = false;
        m_held.store(false);
    }

    /// True if this thread must wait at a gate before its loop.
    bool Enabled() const { return m_attached; }

    /// Thread side. Returns true if the gate opened; see StartGate::Wait().
    /// The gate is not touched after this returns.
    bool Wait(const std::atomic<bool>& exit)
    {
        m_attached = false;
        return m_gate->Wait(exit, m_ticket, *m_wake);
    }

    /// Thread side, after Wait() and after refreshing the watchdog heartbeat:
    /// no longer held.
    void Passed() { m_held.store(false); }

    /// ExitThread(): wake this thread if it is waiting at its gate. Uses only
    /// this object, never the gate, so a destroyed gate is never touched.
    void Wake()
    {
        if (m_wake && m_held.load())
            m_wake->Signal();
    }

    /// Held at the gate: the watchdog skips the thread.
    bool IsHeld() const { return m_held.load(); }

private:
    StartGate* m_gate = nullptr;
    std::atomic<bool> m_held{false};
    bool m_attached = false;
    StartGate::Ticket m_ticket;
    std::optional<dmq::Semaphore> m_wake;
};

} // namespace dmq::os

#endif // DMQ_HAS_START_GATE && DMQ_HAS_SEMAPHORE

#endif // _DMQ_START_GATE_H
