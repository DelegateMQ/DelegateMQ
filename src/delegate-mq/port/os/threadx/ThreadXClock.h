#ifndef THREADX_CLOCK_H
#define THREADX_CLOCK_H

#include "tx_api.h"
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

            // tx_time_get() counts ThreadX ticks (TX_TIMER_TICKS_PER_SECOND
            // per second), not milliseconds -- scale before returning, or
            // every duration/deadline computed against this clock (e.g.
            // dmq::util::Timer expirations) would run TX_TIMER_TICKS_PER_SECOND/1000x
            // slower than requested.
            uint64_t ms = (ticks * 1000ULL) / TX_TIMER_TICKS_PER_SECOND;

            return time_point(duration(static_cast<rep>(ms)));
        }
    };
} // namespace dmq::os

#endif // THREADX_CLOCK_H