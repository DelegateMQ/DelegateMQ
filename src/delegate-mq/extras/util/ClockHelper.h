#ifndef CLOCK_HELPER_H
#define CLOCK_HELPER_H

#include "../../delegate/DelegateOpt.h"
#include <chrono>

namespace dmq::util {

namespace detail {
    /// @brief Captures the process-start time point once, on first call.
    inline const dmq::Clock::time_point& ProcessStart() {
        static const dmq::Clock::time_point start = dmq::Clock::now();
        return start;
    }
}

/// @brief Get elapsed time since process start in milliseconds.
/// @details Normalizes to process start so timestamps are comparable across
/// nodes regardless of the underlying clock's epoch (e.g. FreeRTOS ticks
/// vs. std::chrono::steady_clock which counts from system boot).
/// @return Milliseconds elapsed since first call to any dmq::util::Timestamp function.
inline uint64_t TimestampMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        dmq::Clock::now() - detail::ProcessStart()).count());
}

/// @brief Get elapsed time since process start in microseconds.
/// @return Microseconds elapsed since first call to any dmq::util::Timestamp function.
inline uint64_t TimestampUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        dmq::Clock::now() - detail::ProcessStart()).count());
}

} // namespace dmq::util

#endif
