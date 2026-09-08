#ifndef ZEPHYR_THIS_THREAD_H
#define ZEPHYR_THIS_THREAD_H

/// @file ZephyrThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, Zephyr backend.
///
/// @details
/// Backs dmq::ThisThread::sleep_for()/yield() (see DelegateOpt.h). Exists so
/// library internals that need to delay or yield (e.g. RetryMonitor backoff)
/// aren't forced to pull in the full dmq::os::Thread class -- which also
/// drags in the message queue, watchdog, and stats machinery -- just to
/// sleep. dmq::os::Thread::Sleep() forwards here too, so the Zephyr delay
/// call is implemented exactly once.

#include <zephyr/kernel.h>
#include <chrono>

namespace dmq::os {

    struct ZephyrThisThread {
        static void sleep_for(std::chrono::milliseconds ms) {
            k_sleep(K_MSEC(ms.count()));
        }

        static void yield() noexcept {
            k_yield();
        }
    };

} // namespace dmq::os

#endif // ZEPHYR_THIS_THREAD_H
