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
/// - The gate must outlive every thread's wait at it (destroying a gate with
///   threads still holding faults). Once its threads are released it may be
///   destroyed, e.g. a local in a startup function; but a thread re-created
///   later reads its gate again, so call `SetStartGate(nullptr)` first.
///
/// Supported where `DMQ_HAS_START_GATE` is defined (delegate/DelegateSelect.h):
/// every thread port (not bare metal, which has no threads).

#include "delegate/DelegateOpt.h"
#include "delegate/Semaphore.h"
#include <atomic>
#include <cstdint>

#if defined(DMQ_HAS_START_GATE) && defined(DMQ_HAS_SEMAPHORE)

namespace dmq::os {

class StdlibThread;
class Win32Thread;
class PosixThread;
class QtThread;
class FreeRTOSThread;
class ThreadXThread;
class ZephyrThread;
class CmsisRtos2Thread;
class NuttXThread;

class StartGate
{
public:
    StartGate() = default;

    ~StartGate()
    {
        // A thread still holding would wake on a destroyed gate.
        DMQ_ASSERT_TRUE(m_waiters == nullptr);
    }

    /// Release every thread created with this gate so far, whether it is
    /// already waiting or still on its way to the gate. Threads created later
    /// pass straight through until Close().
    void Open()
    {
        dmq::LockGuard<dmq::Mutex> lock(m_mutex);
        m_open = true;
        m_openCount++;
        for (Waiter* w = m_waiters; w != nullptr; w = w->next)
            w->sem.Signal();
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
    friend class StdlibThread;
    friend class Win32Thread;
    friend class PosixThread;
    friend class QtThread;
    friend class FreeRTOSThread;
    friend class ThreadXThread;
    friend class ZephyrThread;
    friend class CmsisRtos2Thread;
    friend class NuttXThread;

    /// One per held thread, on that thread's stack; the gate itself allocates
    /// nothing (on RTOS ports the semaphore creates OS objects).
    struct Waiter
    {
        dmq::Semaphore sem;
        Waiter* next = nullptr;
    };

    /// Called by a thread port's CreateThread() before it starts the thread.
    /// Pass the result to Wait(): an Open() after this releases that thread
    /// even if Close() follows before the thread reaches the gate.
    uint32_t OpenCount() const
    {
        dmq::LockGuard<dmq::Mutex> lock(m_mutex);
        return m_openCount;
    }

    /// Open now, or opened since `openCountAtCreate` (from OpenCount()).
    bool Released(uint32_t openCountAtCreate) const
    {
        return m_open || m_openCount != openCountAtCreate;
    }

    /// Called by a thread port on its own thread, after its start handshake
    /// and before its message loop. Blocks until the gate opens (now or since
    /// `openCountAtCreate`) or `exit` is set (followed by Wake()).
    /// @return true if the gate opened: the thread enters its loop, and an exit
    /// requested meanwhile follows its ExitPolicy (DRAIN runs the queued
    /// messages). false if the thread was told to exit while still held: it
    /// runs nothing.
    bool Wait(const std::atomic<bool>& exit, uint32_t openCountAtCreate)
    {
        // Already open (the usual case after startup): don't create a waiter,
        // whose semaphore is OS objects on the RTOS ports
        {
            dmq::LockGuard<dmq::Mutex> lock(m_mutex);
            if (Released(openCountAtCreate) || exit.load())
                return Released(openCountAtCreate);
        }

        Waiter self;
        {
            // Checked again: the gate may have opened since
            dmq::LockGuard<dmq::Mutex> lock(m_mutex);
            if (Released(openCountAtCreate) || exit.load())
                return Released(openCountAtCreate);
            self.next = m_waiters;
            m_waiters = &self;
        }

        // Open()/Wake() signal under m_mutex. A signal sent before this wait
        // starts is kept by the semaphore, so none is lost.
        bool opened = false;
        for (;;)
        {
            self.sem.Wait(dmq::Duration::max());
            dmq::LockGuard<dmq::Mutex> lock(m_mutex);
            if (exit.load() || Released(openCountAtCreate))
            {
                opened = Released(openCountAtCreate);
                Unlink(&self);
                break;
            }
        }
        return opened;
    }

    /// Called by a thread port's ExitThread() after setting its exit flag:
    /// waiting threads recheck their condition (only the exiting one leaves).
    void Wake()
    {
        dmq::LockGuard<dmq::Mutex> lock(m_mutex);
        for (Waiter* w = m_waiters; w != nullptr; w = w->next)
            w->sem.Signal();
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
    Waiter* m_waiters = nullptr;
};

} // namespace dmq::os

#endif // DMQ_HAS_START_GATE && DMQ_HAS_SEMAPHORE

#endif // _DMQ_START_GATE_H
