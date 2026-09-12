#ifndef NUTTX_THIS_THREAD_H
#define NUTTX_THIS_THREAD_H

/// @file NuttXThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, NuttX backend.
///
/// @details
/// Backs dmq::ThisThread::sleep_for()/yield() (see DelegateOpt.h). Exists so
/// library internals that need to delay or yield (e.g. RetryMonitor backoff)
/// aren't forced to pull in the full dmq::os::Thread class -- which also
/// drags in the message queue, watchdog, and stats machinery -- just to
/// sleep. dmq::os::Thread::Sleep() forwards here too, so the NuttX delay
/// call is implemented exactly once.

#include <time.h>
#include <sched.h>
#include <chrono>

namespace dmq::os {

    struct NuttXThisThread {
        static void sleep_for(std::chrono::milliseconds ms) {
            struct timespec ts;
            ts.tv_sec = static_cast<time_t>(ms.count() / 1000);
            ts.tv_nsec = static_cast<long>((ms.count() % 1000) * 1000000L);
            nanosleep(&ts, nullptr);
        }

        static void yield() noexcept {
            sched_yield();
        }
    };

} // namespace dmq::os

#endif // NUTTX_THIS_THREAD_H
