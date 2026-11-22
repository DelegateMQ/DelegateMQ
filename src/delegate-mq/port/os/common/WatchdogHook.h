#ifndef _DMQ_WATCHDOG_HOOK_H
#define _DMQ_WATCHDOG_HOOK_H

/// @file WatchdogHook.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief The watchdog hook: an application function that Thread::WatchdogCheckAll()
/// calls for each hung thread before it faults, to capture the failure.
///
/// @details DelegateMQ captures nothing itself. The hook gets the hung thread, so the
/// application can do what its platform allows: read the thread's saved registers
/// from the OS (the program counter shows where it's stuck), start a core dump, or
/// write the thread's name to RAM that survives the reset. WatchdogCheckAll() reports
/// every hung thread to the hook (two deadlocked threads are both reported), then
/// calls WatchdogHandler() once, which never returns.
///
/// The hook runs in the task that calls WatchdogCheckAll(), not in the hung thread,
/// while that call holds the watchdog list lock. So the hook must not create or
/// destroy threads, wait on other threads, allocate, or use delegates, signals or the
/// DataBus. A hardware watchdog keeps counting meanwhile: the hook must finish before
/// it fires. It shouldn't try to recover; the fault that follows is the recovery.
///
/// Included by DelegateMQ.h after the selected thread port, which defines
/// dmq::os::Thread. Not available on bare metal (DMQ_THREAD_NONE: no threads).

#include "delegate/DelegateOpt.h"
#include "extras/util/Fault.h"
#include <atomic>

namespace dmq::os {

/// @brief What the watchdog hook gets about one hung thread.
struct WatchdogInfo
{
    /// The hung thread. Its GetThreadId() is the OS's handle (TaskHandle_t on
    /// FreeRTOS, TX_THREAD* on ThreadX, ...), for reading its saved context.
    Thread& thread;

    /// The thread's name, as passed to its constructor.
    const char* threadName;

    /// Time since the thread was last alive (more than its watchdog timeout).
    dmq::Duration stalledFor;
};

/// @brief A watchdog hook: a plain function, not a delegate, so calling it never
/// locks or allocates.
using WatchdogHook = void (*)(const WatchdogInfo& info);

namespace detail {
    inline std::atomic<WatchdogHook> g_watchdogHook{nullptr};
}

/// @brief Set the function Thread::WatchdogCheckAll() calls for each hung thread,
/// before WatchdogHandler(). nullptr removes it. See the file notes for what the
/// hook may and may not do.
inline void SetWatchdogHook(WatchdogHook hook) noexcept { detail::g_watchdogHook.store(hook); }

namespace detail {

/// @brief One WatchdogCheckAll() pass, for the thread ports: report each expired
/// thread to the hook, then fault once.
class WatchdogReport
{
public:
    /// @brief A watched thread has expired: call the hook, if one is set.
    void Expired(Thread& thread, const char* threadName, dmq::Duration stalledFor)
    {
        if (m_first == nullptr)
            m_first = threadName;
        // A hook that calls WatchdogCheckAll() again (a bug) re-enters here on the
        // same thread: the watchdog lock is recursive. Don't call the hook again.
        static std::atomic<bool> running{false};
        if (running.load())
            return;
        if (WatchdogHook hook = g_watchdogHook.load())
        {
            running.store(true);
            hook(WatchdogInfo{ thread, threadName, stalledFor });
            running.store(false);
        }
    }

    /// @brief After every thread is checked: WatchdogHandler() with the first
    /// expired thread's name, if any expired. It never returns then.
    void Finish() const
    {
        if (m_first != nullptr)
            ::WatchdogHandler(m_first);
    }

private:
    const char* m_first = nullptr;
};

} // namespace detail
} // namespace dmq::os

#endif // _DMQ_WATCHDOG_HOOK_H
