#ifndef DMQ_THREAD_PRIORITY_H
#define DMQ_THREAD_PRIORITY_H

/// @file ThreadPriority.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief dmq::os::ThreadPriority: thread priority as one of seven portable
/// bands, so the same SetPriority() call builds on every thread port. Each
/// port turns a band into its kernel's own number, whichever way the kernel
/// counts (FreeRTOS and NuttX: higher is more urgent; ThreadX and Zephyr:
/// lower is more urgent; CMSIS-RTOS2: osPriority_t).
///
/// - SetPriority(band) on any dmq::os::Thread. The port's native
///   SetThreadPriority() stays, for the rare thread that needs one exact level.
/// - SetPriority(band, step) orders threads within a band: +1 is one native
///   level more urgent, -1 one less. A step never reaches the next band: one
///   that would faults (DMQ_ASSERT), so the bands keep their order.
/// - Where the bands land is set per project: DMQ_PRIORITY_BAND_* in the
///   config (native numbers), else the port spreads them across its range.
/// - The desktop ports (stdlib, Win32, POSIX, Qt) map the bands to the OS's
///   own thread priorities (port/os/common/DesktopPriority.h), and ignore the
///   step and the DMQ_PRIORITY_BAND_* values.
///
/// Ports include this header directly, as they do StackWatermark.h.

#include "delegate/DelegateOpt.h"
#include <cstdint>

namespace dmq::os {

/// @brief Portable thread priority band, least urgent first. The names follow
/// the Win32 levels (HIGH and LOW are macros in the Arduino core).
enum class ThreadPriority : uint8_t
{
    IDLE,
    LOWEST,
    BELOW_NORMAL,
    NORMAL,
    ABOVE_NORMAL,
    HIGHEST,
    TIME_CRITICAL
};

namespace detail {

inline constexpr int PRIORITY_BAND_COUNT = 7;

/// @brief A port's bands as native priorities, least urgent first, and the
/// least and most urgent native values a step may reach. most < least where a
/// lower number is more urgent (ThreadX, Zephyr).
struct PriorityBands
{
    int band[PRIORITY_BAND_COUNT];
    int least;
    int most;
};

/// @brief Bands spread across a port's range: IDLE at least, NORMAL at normal,
/// TIME_CRITICAL at most, the others evenly between.
constexpr PriorityBands SpreadPriorityBands(int least, int normal, int most)
{
    PriorityBands b{};
    for (int r = 0; r < PRIORITY_BAND_COUNT; r++) {
        b.band[r] = r <= 3 ? least + (normal - least) * r / 3
                           : normal + (most - normal) * (r - 3) / 3;
    }
    b.least = least;
    b.most = most;
    return b;
}

/// @brief The port's bands with the ones set in the config (DMQ_PRIORITY_BAND_*,
/// anything but dmq::PRIORITY_BAND_AUTO) in their place.
inline PriorityBands ApplyPriorityBandConfig(PriorityBands b,
    const int (&config)[PRIORITY_BAND_COUNT] = dmq::PRIORITY_BANDS)
{
    for (int r = 0; r < PRIORITY_BAND_COUNT; r++) {
        if (config[r] != dmq::PRIORITY_BAND_AUTO)
            b.band[r] = config[r];
    }
    return b;
}

/// @brief How urgent a native priority is: larger is more urgent on any port.
inline long long PriorityUrgency(const PriorityBands& b, int native)
{
    return b.most >= b.least ? static_cast<long long>(native) : -static_cast<long long>(native);
}

/// @brief The native priority for band + step.
/// @return false if the bands are out of order or outside [least, most], or
/// the step would leave its band (reach the next band, or pass least/most).
inline bool ResolvePriority(const PriorityBands& b, ThreadPriority band, int step, int& native)
{
    const long long leastU = PriorityUrgency(b, b.least);
    const long long mostU = PriorityUrgency(b, b.most);
    for (int r = 0; r < PRIORITY_BAND_COUNT; r++) {
        const long long u = PriorityUrgency(b, b.band[r]);
        if (u < leastU || u > mostU)
            return false;
        if (r > 0 && u < PriorityUrgency(b, b.band[r - 1]))
            return false;
    }

    const int r = static_cast<int>(band);
    if (r < 0 || r >= PRIORITY_BAND_COUNT)
        return false;

    const long long target = PriorityUrgency(b, b.band[r]) + step;
    if (step > 0) {
        const long long limit = r + 1 < PRIORITY_BAND_COUNT ? PriorityUrgency(b, b.band[r + 1]) - 1 : mostU;
        if (target > limit)
            return false;
    }
    else if (step < 0) {
        const long long limit = r > 0 ? PriorityUrgency(b, b.band[r - 1]) + 1 : leastU;
        if (target < limit)
            return false;
    }
    native = static_cast<int>(b.most >= b.least ? target : -target);
    return true;
}

/// @brief The native priority for band + step on a port, with the config's
/// bands applied. Faults (DMQ_ASSERT) if the bands are out of order or the
/// step would leave its band.
inline int PriorityToNative(const PriorityBands& portBands, ThreadPriority band, int step)
{
    int native = 0;
    const bool ok = ResolvePriority(ApplyPriorityBandConfig(portBands), band, step, native);
    DMQ_ASSERT_TRUE(ok);
    return native;
}

} // namespace detail

} // namespace dmq::os

#endif // DMQ_THREAD_PRIORITY_H
