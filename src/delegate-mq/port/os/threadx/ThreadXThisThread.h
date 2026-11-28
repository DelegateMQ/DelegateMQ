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
#include "ThreadXClock.h"
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

        /// @brief Sleep until dmq::Clock reaches t; returns at once if it
        /// already has. For drift-free periodic loops: next += period;
        /// sleep_until(next).
        /// @note ThreadX has no absolute sleep: this computes the ticks left
        /// and calls tx_thread_sleep(), so a preemption between the two delays
        /// the wake by as long as it lasts. The next period still starts from
        /// the absolute target, so the delay doesn't accumulate.
        template<typename Duration>
        static void sleep_until(const std::chrono::time_point<ThreadXClock, Duration>& t) {
            // Target in ticks, rounded up so the wake is never early
            const int64_t raw = static_cast<int64_t>(
                std::chrono::ceil<std::chrono::milliseconds>(t.time_since_epoch()).count());
            if (raw <= 0)
                return;     // before the clock's start: already past
            const uint64_t ms = static_cast<uint64_t>(raw);
            // Split so a far-future time point can't overflow
            const uint64_t tps = TX_TIMER_TICKS_PER_SECOND;
            const uint64_t target = (ms / 1000u) * tps + ((ms % 1000u) * tps + 999u) / 1000u;
            constexpr uint64_t MAX_STEP = 0x7FFFFFFFu;
            for (;;) {
                const uint64_t now = ThreadXClock::NowTicks();
                if (now >= target)
                    return;
                const uint64_t remaining = target - now;
                tx_thread_sleep(static_cast<ULONG>(remaining > MAX_STEP ? MAX_STEP : remaining));
            }
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
