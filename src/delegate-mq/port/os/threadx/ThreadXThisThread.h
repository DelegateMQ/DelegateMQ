#ifndef THREADX_THIS_THREAD_H
#define THREADX_THIS_THREAD_H

/// @file ThreadXThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, ThreadX backend.
///
/// @details
/// Backs dmq::ThisThread::sleep_for()/yield() (see DelegateOpt.h). Exists so
/// library internals that need to delay or yield (e.g. RetryMonitor backoff)
/// aren't forced to pull in the full dmq::os::Thread class -- which also
/// drags in the message queue, watchdog, and stats machinery -- just to
/// sleep. dmq::os::Thread::Sleep() forwards here too, so the ThreadX delay
/// call is implemented exactly once.

#include <tx_api.h>
#include <chrono>

namespace dmq::os {

    struct ThreadXThisThread {
        static void sleep_for(std::chrono::milliseconds ms) {
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
