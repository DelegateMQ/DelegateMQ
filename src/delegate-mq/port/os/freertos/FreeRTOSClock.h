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
            // Static state to track the tick rollover.
            // 1. It must be called at least once per rollover period to detect the wrap.
            // 2. Thread safety: Protected by Critical Section below.
            static TickType_t last = 0;
            static uint64_t high = 0;

            // Enter Critical Section
            // This prevents context switches and interrupts during the state update.
            // NOTE: This function must be called from a TASK, not an ISR.
            taskENTER_CRITICAL();

            TickType_t cur = xTaskGetTickCount();

            // Determine the rollover modulus based on the width of TickType_t.
            // This makes the logic portable for both 16-bit and 32-bit FreeRTOS ticks.
            constexpr uint64_t TICK_MODULO = static_cast<uint64_t>(1ULL) << (sizeof(TickType_t) * 8);

            // Check for wrap-around
            if (cur < last) {
                high += TICK_MODULO;
            }

            last = cur;

            // Combine high part with current tick
            uint64_t ticks = high + cur;

            taskEXIT_CRITICAL();  // End Lock

            return time_point(duration(static_cast<rep>(ticks)));
        }
    };
} // namespace dmq::os

#endif // FREERTOS_CLOCK_H