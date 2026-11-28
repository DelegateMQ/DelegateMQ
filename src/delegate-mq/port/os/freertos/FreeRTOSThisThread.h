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

#include "FreeRTOSHeaders.h"
#include "FreeRTOSMutex.h"
#include "FreeRTOSCriticalSection.h"
#include "FreeRTOSClock.h"
#include "port/os/common/CurrentThreadStorage.h"
#include <chrono>

namespace dmq::os {

    /// @brief Key for RegistryCurrentThread: the calling task's handle, or 0
    /// inside an ISR (detected on ARM Cortex-M and on ESP-IDF, ESP32 Xtensa and
    /// RISC-V; see FreeRTOSCriticalSection.h).
    /// @TODO: On another non-ARM FreeRTOS port (a non-ESP-IDF RISC-V, ...), extend
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

        /// @brief Sleep until the tick dmq::Clock reaches t; returns at once if
        /// it already has. For drift-free periodic loops: next += period;
        /// sleep_until(next). Uses xTaskDelayUntil() (absolute: a preemption
        /// before the call doesn't delay the wake) when INCLUDE_xTaskDelayUntil
        /// or INCLUDE_vTaskDelayUntil is 1, else vTaskDelay() for the rest.
        template<typename Duration>
        static void sleep_until(const std::chrono::time_point<FreeRTOSClock, Duration>& t) {
            const int64_t raw = static_cast<int64_t>(
                std::chrono::ceil<FreeRTOSClock::duration>(t.time_since_epoch()).count());
            if (raw <= 0)
                return;     // before the clock's start: already past
            const uint64_t target = static_cast<uint64_t>(raw);
            // The longest single delay TickType_t can express without
            // ambiguity; a longer wait goes in steps
            constexpr uint64_t MAX_STEP = static_cast<uint64_t>(portMAX_DELAY) / 2u;
            for (;;) {
                const uint64_t now = FreeRTOSClock::NowTicks();
                if (now >= target)
                    return;
                const uint64_t remaining = target - now;
                const TickType_t step = static_cast<TickType_t>(remaining > MAX_STEP ? MAX_STEP : remaining);
#if defined(INCLUDE_xTaskDelayUntil) && (INCLUDE_xTaskDelayUntil == 1)
                TickType_t from = static_cast<TickType_t>(now);
                (void)xTaskDelayUntil(&from, step);
#elif defined(INCLUDE_vTaskDelayUntil) && (INCLUDE_vTaskDelayUntil == 1)
                TickType_t from = static_cast<TickType_t>(now);
                vTaskDelayUntil(&from, step);
#else
                vTaskDelay(step);
#endif
            }
        }

        static void yield() noexcept {
            taskYIELD();
        }
    };

} // namespace dmq::os

#endif // FREERTOS_THIS_THREAD_H
