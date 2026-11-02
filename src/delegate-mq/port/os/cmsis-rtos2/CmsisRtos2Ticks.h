#ifndef CMSIS_RTOS2_TICKS_H
#define CMSIS_RTOS2_TICKS_H

/// @file CmsisRtos2Ticks.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Converts between std::chrono durations and CMSIS-RTOS2 kernel ticks.
///
/// @details
/// Every CMSIS-RTOS2 timeout (osSemaphoreAcquire, osMessageQueuePut/Get,
/// osDelay, ...) and osKernelGetTickCount() are in kernel ticks, and the tick
/// rate is whatever the kernel is configured for (osKernelGetTickFreq()), not
/// necessarily 1 kHz. Every place in this port that passes a timeout to the
/// kernel or reads the tick count goes through these helpers.
///
/// Included from DelegateOpt.h's early port-include block (before namespace
/// dmq opens), so it doesn't use dmq::Duration; the helpers are templated on
/// the chrono duration type instead, like CmsisRtos2Semaphore::Wait().

#include "cmsis_os2.h"
#include <chrono>
#include <cstdint>

namespace dmq::os {

    /// @return the kernel tick rate in Hz (1000 if the kernel reports 0).
    inline uint32_t CmsisRtos2TickFreq() noexcept {
        uint32_t freq = osKernelGetTickFreq();
        return freq != 0 ? freq : 1000u;
    }

    /// @brief Convert a duration to a CMSIS-RTOS2 timeout in kernel ticks.
    /// @details Rounds up, so a nonzero timeout never becomes 0 (a non-blocking
    /// call) and never waits less than asked. duration::max() maps to
    /// osWaitForever; a zero or negative duration maps to 0. A finite timeout
    /// too long for 32 bits saturates just below osWaitForever.
    template<typename Rep, typename Period>
    uint32_t CmsisRtos2ToTicks(const std::chrono::duration<Rep, Period>& d) noexcept {
        using DurationT = std::chrono::duration<Rep, Period>;
        if (d == DurationT::max())
            return osWaitForever;
        if (d <= DurationT::zero())
            return 0;
        constexpr uint64_t maxFinite = static_cast<uint64_t>(osWaitForever) - 1u;
        // Saturate before converting to microseconds, which would overflow
        // int64 for a huge finite seconds/hours value. 2,000,000 hours is more
        // than 2^32 ticks at any tick rate of 1 Hz or above.
        if (d > std::chrono::hours(2000000))
            return static_cast<uint32_t>(maxFinite);
        const auto us = std::chrono::ceil<std::chrono::microseconds>(d).count();
        const uint64_t freq = CmsisRtos2TickFreq();
        if (static_cast<uint64_t>(us) > (UINT64_MAX - 999999u) / freq)
            return static_cast<uint32_t>(maxFinite);
        const uint64_t ticks = (static_cast<uint64_t>(us) * freq + 999999u) / 1000000u;
        return static_cast<uint32_t>(ticks > maxFinite ? maxFinite : ticks);
    }

    /// @brief Convert a kernel tick count to milliseconds.
    inline uint64_t CmsisRtos2TicksToMs(uint64_t ticks) noexcept {
        const uint64_t freq = CmsisRtos2TickFreq();
        if (freq == 1000u)
            return ticks;
        // Split to avoid overflowing ticks * 1000 on a long uptime.
        return (ticks / freq) * 1000u + ((ticks % freq) * 1000u) / freq;
    }

} // namespace dmq::os

#endif // CMSIS_RTOS2_TICKS_H
