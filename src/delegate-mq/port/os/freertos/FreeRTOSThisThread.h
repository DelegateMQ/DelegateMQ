#ifndef FREERTOS_THIS_THREAD_H
#define FREERTOS_THIS_THREAD_H

/// @file FreeRTOSThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, FreeRTOS backend.
///
/// @details
/// Backs dmq::ThisThread::sleep_for()/yield() (see DelegateOpt.h). Exists so
/// library internals that need to delay or yield (e.g. RetryMonitor backoff)
/// aren't forced to pull in the full dmq::os::Thread class -- which also
/// drags in the message queue, watchdog, and stats machinery -- just to
/// sleep. dmq::os::Thread::Sleep() forwards here too, so the FreeRTOS delay
/// call is implemented exactly once.

#include "FreeRTOS.h"
#include "task.h"
#include <chrono>

namespace dmq::os {

    struct FreeRTOSThisThread {
        static void sleep_for(std::chrono::milliseconds ms) {
            vTaskDelay(pdMS_TO_TICKS(ms.count()));
        }

        static void yield() noexcept {
            taskYIELD();
        }
    };

} // namespace dmq::os

#endif // FREERTOS_THIS_THREAD_H
