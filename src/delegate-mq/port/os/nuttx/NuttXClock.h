#ifndef NUTTX_CLOCK_H
#define NUTTX_CLOCK_H

/// @file NuttXClock.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief dmq::Clock backing for NuttX, using the POSIX clock_gettime()
/// call NuttX provides directly (CLOCK_MONOTONIC) rather than a kernel-
/// specific tick counter like k_uptime_get()/tx_time_get(). NuttX's POSIX
/// layer already exposes nanosecond resolution, so no k_ticks-to-duration
/// conversion is needed here.

#include <time.h>
#include <chrono>
#include <cstdint>

namespace dmq::os {
    struct NuttXClock {
        using rep = int64_t;
        using period = std::nano;
        using duration = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<NuttXClock>;
        static const bool is_steady = true;

        static time_point now() noexcept {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            rep ns = static_cast<rep>(ts.tv_sec) * 1000000000LL + static_cast<rep>(ts.tv_nsec);
            return time_point(duration(ns));
        }
    };
} // namespace dmq::os

#endif // NUTTX_CLOCK_H
