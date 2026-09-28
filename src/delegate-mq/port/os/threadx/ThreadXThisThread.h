#ifndef THREADX_THIS_THREAD_H
#define THREADX_THIS_THREAD_H

/// @file ThreadXThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, ThreadX backend.
///
/// @details
/// This is dmq::ThisThread for this port (see DelegateOpt.h). GetCurrent()
/// uses RegistryCurrentThread, keyed by the calling thread's control block. Exists so
/// library internals that need to delay or yield (e.g. RetryMonitor backoff)
/// aren't forced to pull in the full dmq::os::Thread class -- which also
/// drags in the message queue, watchdog, and stats machinery -- just to
/// sleep. dmq::os::Thread::Sleep() forwards here too, so the ThreadX delay
/// call is implemented exactly once.

#include <tx_api.h>
#include "ThreadXMutex.h"
#include "port/os/common/CurrentThreadStorage.h"
#include <chrono>

namespace dmq::os {

    /// @brief Key for RegistryCurrentThread: the calling thread's control block.
    /// @TODO: Return 0 inside an ISR if your target can detect it (e.g. read IPSR on
    /// ARM Cortex-M). tx_thread_identify() returns the interrupted thread there, so
    /// dmq::ThisThread::GetCurrent() from an ISR returns that thread's IThread
    /// instead of nullptr. See docs/PORTING.md, "Current Thread".
    inline uintptr_t ThreadXCurrentThreadKey() {
        return reinterpret_cast<uintptr_t>(tx_thread_identify());
    }

    struct ThreadXThisThread : RegistryCurrentThread<ThreadXCurrentThreadKey, ThreadXMutex> {
        template<typename Rep, typename Period>
        static void sleep_for(std::chrono::duration<Rep, Period> d) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);
            auto count = ms.count();
            // Round up so a sub-tick sleep still yields the CPU for at least
            // one tick rather than returning immediately.
            ULONG ticks = static_cast<ULONG>((count * TX_TIMER_TICKS_PER_SECOND) / 1000);
            if (ticks == 0 && count > 0) ticks = 1;
            tx_thread_sleep(ticks);
        }

        static void yield() noexcept {
            // ThreadX has no separate "yield" API -- relinquish is the
            // documented way to hand the CPU to other ready threads of the
            // same priority.
            tx_thread_relinquish();
        }
    };

} // namespace dmq::os

#endif // THREADX_THIS_THREAD_H
