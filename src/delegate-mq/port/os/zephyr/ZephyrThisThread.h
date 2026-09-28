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

        static void yield() noexcept {
            k_yield();
        }
    };

} // namespace dmq::os

#endif // ZEPHYR_THIS_THREAD_H
