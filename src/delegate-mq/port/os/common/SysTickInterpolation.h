#ifndef DMQ_SYSTICK_INTERPOLATION_H
#define DMQ_SYSTICK_INTERPOLATION_H

/// @file SysTickInterpolation.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Sub-tick time on ARM Cortex-M: the kernel tick count plus how far
/// the SysTick timer has counted toward the next tick.
///
/// @details
/// Used by an RTOS port's HighResClock (e.g. FreeRTOSClock.h) when SysTick
/// drives the kernel tick, the default on Cortex-M. The resolution is one CPU
/// clock, on every Cortex-M (M0 included, which has no DWT cycle counter).
/// SysTick keeps counting while the core sleeps (WFI), so idle time counts too,
/// and QEMU emulates it.
///
/// Call SysTickMicros() with interrupts masked, after reading the tick count:
/// a SysTick that wrapped since then is pending (ICSR.PENDSTSET), so the tick
/// count is one behind and the reading adds that tick.
///
/// @note If SysTick isn't the kernel tick (an RTOS ticked by another timer, at
/// another rate), the fraction is wrong by up to one tick. Define
/// DMQ_NO_SYSTICK_INTERPOLATION to make HighResClock tick-resolution there.
/// If SysTick isn't running with its interrupt enabled, the fraction is 0.

#include <cstdint>

#if defined(__ARM_ARCH_PROFILE) && (__ARM_ARCH_PROFILE == 'M') && !defined(DMQ_NO_SYSTICK_INTERPOLATION)
    #define DMQ_SYSTICK_INTERPOLATION 1
#endif

#if defined(DMQ_SYSTICK_INTERPOLATION)

namespace dmq::os::detail {

    inline volatile uint32_t& SysTickReg(uintptr_t address) {
        return *reinterpret_cast<volatile uint32_t*>(address);
    }

    /// @brief Microseconds since tick 0, interpolated within the current tick.
    /// @param ticks Kernel tick count, read with interrupts masked (still masked now)
    /// @param ticksPerSecond Kernel tick rate (the SysTick interrupt rate)
    inline uint64_t SysTickMicros(uint64_t ticks, uint32_t ticksPerSecond) noexcept {
        constexpr uintptr_t SYST_CSR = 0xE000E010u;    // SysTick control and status
        constexpr uintptr_t SYST_RVR = 0xE000E014u;    // reload value
        constexpr uintptr_t SYST_CVR = 0xE000E018u;    // current value (counts down)
        constexpr uintptr_t SCB_ICSR = 0xE000ED04u;    // interrupt control and state
        constexpr uint32_t CSR_ENABLE_TICKINT = 0x3u;
        constexpr uint32_t ICSR_PENDSTSET = 1u << 26;

        uint64_t us = (ticks * 1000000u) / ticksPerSecond;
        if ((SysTickReg(SYST_CSR) & CSR_ENABLE_TICKINT) != CSR_ENABLE_TICKINT)
            return us;

        const uint32_t load = SysTickReg(SYST_RVR) & 0x00FFFFFFu;
        uint32_t val = SysTickReg(SYST_CVR) & 0x00FFFFFFu;
        if (SysTickReg(SCB_ICSR) & ICSR_PENDSTSET) {
            // Wrapped since the tick count was read: count that tick, and read
            // the counter again (it may have been read before the wrap)
            val = SysTickReg(SYST_CVR) & 0x00FFFFFFu;
            us = ((ticks + 1u) * 1000000u) / ticksPerSecond;
        }
        if (val > load)
            val = load;     // reload changed under us: treat as the tick's start
        // The fraction of the tick in microseconds, exact for any tick rate
        // (load < 2^24: the numerator stays under 2^44)
        us += (static_cast<uint64_t>(load - val) * 1000000u) /
              ((static_cast<uint64_t>(load) + 1u) * ticksPerSecond);
        return us;
    }

} // namespace dmq::os::detail

#endif // DMQ_SYSTICK_INTERPOLATION

#endif // DMQ_SYSTICK_INTERPOLATION_H
