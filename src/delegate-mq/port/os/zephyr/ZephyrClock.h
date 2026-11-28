#ifndef ZEPHYR_CLOCK_H
#define ZEPHYR_CLOCK_H

#include <zephyr/kernel.h>
#include <chrono>

namespace dmq::os {
    struct ZephyrClock {
        // Zephyr k_uptime_get() returns milliseconds (int64_t)
        using rep = int64_t;
        using period = std::milli;
        using duration = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<ZephyrClock>;
        static const bool is_steady = true;

        static time_point now() noexcept {
            return time_point(duration(k_uptime_get()));
        }
    };

    /// @brief dmq::HighResClock for Zephyr: microseconds from the hardware
    /// cycle counter (k_cycle_get_64(), or k_cycle_get_32() extended to 64
    /// bits), for timing short spans (ThreadStats dispatch latency and run
    /// time). ISR-safe.
    /// @note Without CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER the 32-bit counter
    /// wraps (every 43 s at 100 MHz); a wrap is seen only if now() is called
    /// at least once per wrap period, so a span measured across an unseen wrap
    /// is short by a wrap period. Spans shorter than one period are exact.
    struct ZephyrHighResClock {
        using rep = int64_t;
        using period = std::micro;
        using duration = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<ZephyrHighResClock>;
        static const bool is_steady = true;

        static time_point now() noexcept {
#if defined(CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER)
            const uint64_t cycles = k_cycle_get_64();
#else
            static uint32_t last = 0;
            static uint64_t high = 0;
            const unsigned int key = irq_lock();
            const uint32_t cur = k_cycle_get_32();
            if (cur < last)
                high += 0x100000000ULL;
            last = cur;
            const uint64_t cycles = high + cur;
            irq_unlock(key);
#endif
            return time_point(duration(static_cast<rep>(k_cyc_to_us_floor64(cycles))));
        }
    };
} // namespace dmq::os

#endif // ZEPHYR_CLOCK_H