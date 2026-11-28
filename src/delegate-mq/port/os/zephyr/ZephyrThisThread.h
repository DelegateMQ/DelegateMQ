#ifndef ZEPHYR_THIS_THREAD_H
#define ZEPHYR_THIS_THREAD_H

/// @file ZephyrThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, Zephyr backend.
///
/// @details
/// This is dmq::ThisThread for this port (see DelegateOpt.h). GetCurrent()
/// uses RegistryCurrentThread, keyed by the calling thread's id. Exists so
/// library internals that need to delay or yield (e.g. RetryMonitor backoff)
/// aren't forced to pull in the full dmq::os::Thread class -- which also
/// drags in the message queue, watchdog, and stats machinery -- just to
/// sleep. dmq::os::Thread::Sleep() forwards here too, so the Zephyr delay
/// call is implemented exactly once.

#include <zephyr/kernel.h>
#include "ZephyrMutex.h"
#include "ZephyrClock.h"
#include "port/os/common/CurrentThreadStorage.h"
#include <chrono>

namespace dmq::os {

    /// @brief Key for RegistryCurrentThread: the calling thread's id, or 0 inside an ISR.
    inline uintptr_t ZephyrCurrentThreadKey() {
        if (k_is_in_isr())
            return 0;
        return reinterpret_cast<uintptr_t>(k_current_get());
    }

    struct ZephyrThisThread : RegistryCurrentThread<ZephyrCurrentThreadKey, ZephyrMutex> {
        template<typename Rep, typename Period>
        static void sleep_for(std::chrono::duration<Rep, Period> d) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);
            k_sleep(K_MSEC(ms.count()));
        }

        /// @brief Sleep until dmq::Clock (k_uptime_get()) reaches t; returns at
        /// once if it already has. For drift-free periodic loops: next +=
        /// period; sleep_until(next). An absolute kernel timeout
        /// (K_TIMEOUT_ABS_MS) with CONFIG_TIMEOUT_64BIT (Zephyr's default), so
        /// a preemption before the call doesn't delay the wake; without it, a
        /// relative sleep for the time left.
        template<typename Duration>
        static void sleep_until(const std::chrono::time_point<ZephyrClock, Duration>& t) {
            const int64_t target = static_cast<int64_t>(
                std::chrono::ceil<std::chrono::milliseconds>(t.time_since_epoch()).count());
            // A loop: an early wake (k_wakeup()) sleeps again
            for (int64_t now = k_uptime_get(); now < target; now = k_uptime_get()) {
#if defined(CONFIG_TIMEOUT_64BIT)
                k_sleep(K_TIMEOUT_ABS_MS(target));
#else
                // K_MSEC() takes 32 bits: a longer wait goes in steps
                constexpr int64_t MAX_STEP = 0x7FFFFFFF;
                const int64_t left = target - now;
                k_sleep(K_MSEC(static_cast<int32_t>(left > MAX_STEP ? MAX_STEP : left)));
#endif
            }
        }

        static void yield() noexcept {
            k_yield();
        }
    };

} // namespace dmq::os

#endif // ZEPHYR_THIS_THREAD_H
