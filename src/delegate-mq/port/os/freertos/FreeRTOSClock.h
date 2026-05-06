#ifndef FREERTOS_CLOCK_H
#define FREERTOS_CLOCK_H

#include "FreeRTOS.h"
#include "task.h"
#include <chrono>

namespace dmq::os {
    struct FreeRTOSClock {
        // 1. Define duration traits.
        // NOTE: This assumes configTICK_RATE_HZ is 1000 (1ms ticks).
        // If your system uses a different tick rate, change std::milli to std::ratio<1, configTICK_RATE_HZ>.
        using rep = int64_t;
        using period = std::milli;
        using duration = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<FreeRTOSClock>;
        static const bool is_steady = true;

        // 2. The critical "now()" function
        static time_point now() noexcept {
            // xTaskGetTickCount() is a single atomic 32-bit read on 32-bit FreeRTOS —
            // no locking required. All uses in this codebase are delta computations
            // (watchdog, timer expiry, latency) over intervals well under 49.7 days,
            // so uint32_t modular wrap is never a concern.
            return time_point(duration(static_cast<rep>(xTaskGetTickCount())));
        }
    };
} // namespace dmq::os

#endif // FREERTOS_CLOCK_H