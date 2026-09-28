#ifndef NUTTX_THIS_THREAD_H
#define NUTTX_THIS_THREAD_H

/// @file NuttXThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, NuttX backend.
///
/// @details
/// This is dmq::ThisThread for this port (see DelegateOpt.h). GetCurrent()
/// uses RegistryCurrentThread, keyed by the calling thread's id. Exists so
/// library internals that need to delay or yield (e.g. RetryMonitor backoff)
/// aren't forced to pull in the full dmq::os::Thread class -- which also
/// drags in the message queue, watchdog, and stats machinery -- just to
/// sleep. dmq::os::Thread::Sleep() forwards here too, so the NuttX delay
/// call is implemented exactly once.

#include <time.h>
#include <sched.h>
#include "NuttXMutex.h"
#include <nuttx/arch.h>
#include <pthread.h>
#include "port/os/common/CurrentThreadStorage.h"
#include <chrono>

namespace dmq::os {

    /// @brief Key for RegistryCurrentThread: the calling thread's id plus one (pid 0
    /// is the idle task, and 0 means "none"), or 0 inside an interrupt handler.
    inline uintptr_t NuttXCurrentThreadKey() {
        if (up_interrupt_context())
            return 0;
        return static_cast<uintptr_t>(pthread_self()) + 1;
    }

    struct NuttXThisThread : RegistryCurrentThread<NuttXCurrentThreadKey, NuttXMutex> {
        template<typename Rep, typename Period>
        static void sleep_for(std::chrono::duration<Rep, Period> d) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);
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
