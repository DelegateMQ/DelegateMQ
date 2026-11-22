/// @file cortex_m_systick.h
/// Cortex-M SysTick as the 1 ms tick of bsp.h, shared by the Arm targets.
/// No CMSIS-Core: the SysTick registers are memory-mapped at the same address
/// on every Cortex-M. Each target names its own interrupt handler (its vector
/// table decides the name) and calls SysTickTick() from it.

#ifndef CORTEX_M_SYSTICK_H
#define CORTEX_M_SYSTICK_H

#include "DelegateMQ.h"
#include <cstdint>

#define SYST_CSR  (*(volatile uint32_t*)0xE000E010)
#define SYST_RVR  (*(volatile uint32_t*)0xE000E014)
#define SYST_CVR  (*(volatile uint32_t*)0xE000E018)
#define SYST_CSR_ENABLE    (1u << 0)
#define SYST_CSR_TICKINT   (1u << 1)
#define SYST_CSR_CLKSOURCE (1u << 2) // processor clock, not the external reference

/// Start SysTick with a 1 ms period. coreClockHz: the core clock (QEMU's
/// MPS2 boards run their Cortex-M at 25 MHz, as ARM's application notes do).
inline void SysTickStart(uint32_t coreClockHz)
{
    SYST_RVR = coreClockHz / 1000 - 1;
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_ENABLE | SYST_CSR_TICKINT | SYST_CSR_CLKSOURCE;
}

/// Body of the SysTick interrupt handler: a real hardware interrupt, so
/// Timer::ProcessTimers() runs in ISR context here and exercises
/// dmq::os::BareMetalCriticalSection (PRIMASK) as it would on a real board.
inline void SysTickTick()
{
    g_ticks = g_ticks + 1;
    dmq::util::Timer::ProcessTimers();
}

#endif // CORTEX_M_SYSTICK_H
