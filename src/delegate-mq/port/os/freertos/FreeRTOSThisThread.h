#ifndef FREERTOS_THIS_THREAD_H
#define FREERTOS_THIS_THREAD_H

/// @file FreeRTOSThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, FreeRTOS backend.
///
/// @details
/// This is dmq::ThisThread for this port (see DelegateOpt.h). GetCurrent()
/// uses RegistryCurrentThread, keyed by the calling task's handle. Exists so
/// library internals that need to delay or yield (e.g. RetryMonitor backoff)
/// aren't forced to pull in the full dmq::os::Thread class -- which also
/// drags in the message queue, watchdog, and stats machinery -- just to
/// sleep. dmq::os::Thread::Sleep() forwards here too, so the FreeRTOS delay
/// call is implemented exactly once.

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOSMutex.h"
#include "FreeRTOSCriticalSection.h"
#include "port/os/common/CurrentThreadStorage.h"
#include <chrono>

namespace dmq::os {

    /// @brief Key for RegistryCurrentThread: the calling task's handle, or 0
    /// inside an ISR (detected on ARM Cortex-M; see FreeRTOSCriticalSection.h).
    /// @TODO: On a non-ARM FreeRTOS port (RISC-V, Xtensa, ...), extend
    /// detail::IsInsideFreeRTOSInterrupt() for that architecture; until then,
    /// dmq::ThisThread::GetCurrent() from an ISR returns the interrupted task's
    /// IThread instead of nullptr. See docs/PORTING.md, "Current Thread".
    inline uintptr_t FreeRTOSCurrentThreadKey() {
        if (detail::IsInsideFreeRTOSInterrupt())
            return 0;
        return reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
    }

    struct FreeRTOSThisThread : RegistryCurrentThread<FreeRTOSCurrentThreadKey, FreeRTOSMutex> {
        template<typename Rep, typename Period>
        static void sleep_for(std::chrono::duration<Rep, Period> d) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);
            vTaskDelay(pdMS_TO_TICKS(ms.count()));
        }

        static void yield() noexcept {
            taskYIELD();
        }
    };

} // namespace dmq::os

#endif // FREERTOS_THIS_THREAD_H
