#ifndef CMSIS_RTOS2_THIS_THREAD_H
#define CMSIS_RTOS2_THIS_THREAD_H

/// @file CmsisRtos2ThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, CMSIS-RTOS2 backend.
///
/// @details
/// Backs dmq::ThisThread::sleep_for()/yield() (see DelegateOpt.h). Exists so
/// library internals that need to delay or yield (e.g. RetryMonitor backoff)
/// aren't forced to pull in the full dmq::os::Thread class -- which also
/// drags in the message queue, watchdog, and stats machinery -- just to
/// sleep. dmq::os::Thread::Sleep() forwards here too, so the CMSIS-RTOS2
/// delay call is implemented exactly once.

#include "cmsis_os2.h"
#include <chrono>

namespace dmq::os {

    struct CmsisRtos2ThisThread {
        static void sleep_for(std::chrono::milliseconds ms) {
            osDelay(static_cast<uint32_t>(ms.count()));
        }

        static void yield() noexcept {
            osThreadYield();
        }
    };

} // namespace dmq::os

#endif // CMSIS_RTOS2_THIS_THREAD_H
