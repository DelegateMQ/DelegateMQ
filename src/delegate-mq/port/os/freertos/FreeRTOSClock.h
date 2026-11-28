#ifndef FREERTOS_CLOCK_H
#define FREERTOS_CLOCK_H

#include "FreeRTOSHeaders.h"
#include "FreeRTOSCriticalSection.h"
#include "port/os/common/SysTickInterpolation.h"
#include <chrono>
#include <cstdint>

#if defined(ESP_PLATFORM)
    #include "esp_timer.h"
#endif

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
            return time_point(duration(static_cast<rep>(NowTicks())));
        }

        /// @brief The kernel tick count, extended to 64 bits (no wrap).
        /// @details Must be called at least once per TickType_t rollover period
        /// (49 days with 32-bit ticks at 1 kHz) to see the wrap.
        static uint64_t NowTicks() noexcept {
            // Thread/ISR safety: protected by the critical section below.
            static TickType_t last = 0;
            static uint64_t high = 0;

            // Timer::ProcessTimers() calls this through Timer::GetNow(), and on a
            // real Cortex-M target ProcessTimers() may run in a hardware ISR (see
            // CLAUDE.md "ISR-Safe Locking"). taskENTER_CRITICAL() and
            // xTaskGetTickCount() are task-only, so use FreeRTOSCriticalSection
            // (which picks taskENTER_CRITICAL[_FROM_ISR] by context) and the
            // matching tick-count call. Nesting inside Timer's own
            // FreeRTOSCriticalSection is safe: each instance saves and restores
            // its own state, released in LIFO order.
            const bool inIsr = detail::IsInsideFreeRTOSInterrupt() != pdFALSE;
            FreeRTOSCriticalSection cs;
            cs.lock();

            TickType_t cur = inIsr ? xTaskGetTickCountFromISR() : xTaskGetTickCount();

            // If TickType_t is 64-bit, we don't need the rollover logic as it essentially never wraps.
            // This also prevents a compilation error (shift count >= width of type).
            if (sizeof(TickType_t) < 8) {
                // Determine the rollover modulus based on the width of TickType_t.
                // This makes the logic portable for both 16-bit and 32-bit FreeRTOS ticks.
                // Note: Use a ternary to ensure we never shift by 64 even during constexpr evaluation.
                constexpr uint32_t SHIFT_AMT = (sizeof(TickType_t) < 8) ? (sizeof(TickType_t) * 8) : 0;
                constexpr uint64_t TICK_MODULO = static_cast<uint64_t>(1ULL) << SHIFT_AMT;

                // Check for wrap-around
                if (cur < last) {
                    high += TICK_MODULO;
                }
            }

            last = cur;

            // Combine high part with current tick
            uint64_t ticks = high + cur;

            cs.unlock();
            return ticks;
        }
    };

    /// @brief dmq::HighResClock for FreeRTOS: microseconds, for timing short
    /// spans (ThreadStats dispatch latency and run time) that the 1 ms tick
    /// clock shows as 0.
    /// @details The source, by platform:
    /// - ESP-IDF: esp_timer_get_time() (1 us, 64-bit).
    /// - FreeRTOS's Win32/POSIX simulators: the host's steady clock.
    /// - ARM Cortex-M: the tick count plus SysTick's progress through the
    ///   current tick (SysTickInterpolation.h), resolution one CPU clock.
    /// - Otherwise: the tick count, in microseconds (tick resolution).
    /// ISR-safe. Never goes backward.
    struct FreeRTOSHighResClock {
        using rep = int64_t;
        using period = std::micro;
        using duration = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<FreeRTOSHighResClock>;
        static const bool is_steady = true;

        static time_point now() noexcept {
#if defined(ESP_PLATFORM)
            return time_point(duration(static_cast<rep>(esp_timer_get_time())));
#elif defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
            return time_point(std::chrono::duration_cast<duration>(
                std::chrono::steady_clock::now().time_since_epoch()));
#else
            static uint64_t lastUs = 0;
            FreeRTOSCriticalSection cs;
            cs.lock();
            const uint64_t ticks = FreeRTOSClock::NowTicks();
#if defined(DMQ_SYSTICK_INTERPOLATION)
            uint64_t us = detail::SysTickMicros(ticks, configTICK_RATE_HZ);
#else
            uint64_t us = (ticks * 1000000u) / configTICK_RATE_HZ;
#endif
            // The tick count stands still while the scheduler is suspended
            // (FreeRTOS pends those ticks), while SysTick keeps wrapping:
            // never report an earlier time than last time
            if (us < lastUs)
                us = lastUs;
            lastUs = us;
            cs.unlock();
            return time_point(duration(static_cast<rep>(us)));
#endif
        }
    };
} // namespace dmq::os

#endif // FREERTOS_CLOCK_H
