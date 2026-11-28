#ifndef THREADX_CLOCK_H
#define THREADX_CLOCK_H

#include "tx_api.h"
#include "ThreadXCriticalSection.h"
#include "port/os/common/SysTickInterpolation.h"
#include <chrono>
#include <cstdint>

namespace dmq::os {
    struct ThreadXClock {
        // 1. Define duration traits
        using rep = int64_t;
        using period = std::milli;
        using duration = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<ThreadXClock>;
        static const bool is_steady = true;

        // 2. The critical "now()" function
        static time_point now() noexcept {
            // tx_time_get() counts ThreadX ticks (TX_TIMER_TICKS_PER_SECOND
            // per second), not milliseconds -- scale before returning, or
            // every duration/deadline computed against this clock (e.g.
            // dmq::util::Timer expirations) would run TX_TIMER_TICKS_PER_SECOND/1000x
            // slower than requested.
            uint64_t ms = (NowTicks() * 1000ULL) / TX_TIMER_TICKS_PER_SECOND;

            return time_point(duration(static_cast<rep>(ms)));
        }

        /// @brief tx_time_get() extended to 64 bits (no wrap). Must be called
        /// at least once per ULONG rollover period to see the wrap.
        static uint64_t NowTicks() noexcept {
            static ULONG last = 0;
            static uint64_t high = 0;

            // Enter Critical Section
            // This prevents other threads (and ISRs) from interrupting
            // the read-modify-write of 'last' and 'high'.
            TX_INTERRUPT_SAVE_AREA
            TX_DISABLE

            ULONG cur = tx_time_get();

            constexpr uint64_t TICK_MODULO = static_cast<uint64_t>(1ULL) << (sizeof(ULONG) * 8);

            if (cur < last) {
                high += TICK_MODULO;
            }

            last = cur;

            // Capture value before re-enabling interrupts
            uint64_t ticks = high + cur;

            // Exit Critical Section
            TX_RESTORE
            return ticks;
        }
    };

    /// @brief dmq::HighResClock for ThreadX: microseconds, for timing short
    /// spans (ThreadStats dispatch latency and run time).
    /// @details The source, by platform:
    /// - ThreadX's Linux/Win32 simulation ports: the host's steady clock.
    /// - ARM Cortex-M: the tick count plus SysTick's progress through the
    ///   current tick (SysTickInterpolation.h), when SysTick is the ThreadX
    ///   tick (the default Cortex-M ports' tx_initialize_low_level).
    /// - Otherwise: the tick count, in microseconds (tick resolution).
    /// ISR-safe. Never goes backward.
    struct ThreadXHighResClock {
        using rep = int64_t;
        using period = std::micro;
        using duration = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<ThreadXHighResClock>;
        static const bool is_steady = true;

        static time_point now() noexcept {
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
            return time_point(std::chrono::duration_cast<duration>(
                std::chrono::steady_clock::now().time_since_epoch()));
#else
            static uint64_t lastUs = 0;
            ThreadXCriticalSection cs;
            cs.lock();
            const uint64_t ticks = ThreadXClock::NowTicks();
#if defined(DMQ_SYSTICK_INTERPOLATION)
            uint64_t us = detail::SysTickMicros(ticks, TX_TIMER_TICKS_PER_SECOND);
#else
            uint64_t us = (ticks * 1000000u) / TX_TIMER_TICKS_PER_SECOND;
#endif
            if (us < lastUs)
                us = lastUs;
            lastUs = us;
            cs.unlock();
            return time_point(duration(static_cast<rep>(us)));
#endif
        }
    };
} // namespace dmq::os

#endif // THREADX_CLOCK_H
