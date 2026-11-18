/// @file bsp.cpp
/// Cortex-M0 on QEMU microbit (nRF51822). The nRF51 has no SysTick, so the
/// 1 ms tick is the TIMER0 peripheral: 16 MHz / 2^4 = 1 MHz, compare event
/// every 1000 counts, cleared by hardware (SHORTS), interrupt IRQ 8, wired
/// into startup.c's vector table as TIMER0_IRQHandler.

#include "bsp.h"
#include "DelegateMQ.h"
#include <cstdint>
#include <cstdlib>

namespace {
    constexpr uintptr_t TIMER0 = 0x40008000;
    inline volatile uint32_t& Reg(uintptr_t offset) { return *reinterpret_cast<volatile uint32_t*>(TIMER0 + offset); }

    constexpr uintptr_t TASKS_START       = 0x000;
    constexpr uintptr_t TASKS_CLEAR       = 0x00C;
    constexpr uintptr_t EVENTS_COMPARE0   = 0x140;
    constexpr uintptr_t SHORTS            = 0x200;
    constexpr uintptr_t INTENSET          = 0x304;
    constexpr uintptr_t MODE              = 0x504;
    constexpr uintptr_t BITMODE           = 0x508;
    constexpr uintptr_t PRESCALER         = 0x510;
    constexpr uintptr_t CC0               = 0x540;

    constexpr uint32_t SHORTS_COMPARE0_CLEAR = 1u << 0;
    constexpr uint32_t INTEN_COMPARE0        = 1u << 16;
    constexpr uint32_t TIMER0_IRQ            = 8;

    volatile uint32_t& NVIC_ISER = *reinterpret_cast<volatile uint32_t*>(0xE000E100);
}

const char* BspName() { return "Cortex-M0, arm-none-eabi-gcc, QEMU microbit (nRF51822)"; }

void BspStartTick()
{
    Reg(MODE) = 0;          // timer, not counter
    Reg(BITMODE) = 0;       // 16-bit
    Reg(PRESCALER) = 4;     // 16 MHz / 2^4 = 1 MHz
    Reg(CC0) = 1000;        // 1 ms
    Reg(SHORTS) = SHORTS_COMPARE0_CLEAR;
    Reg(INTENSET) = INTEN_COMPARE0;
    NVIC_ISER = 1u << TIMER0_IRQ;
    Reg(TASKS_CLEAR) = 1;
    Reg(TASKS_START) = 1;
}

// _Exit -> retarget.c's _exit -> semihosting SYS_EXIT with the status
void BspExit(int status) { std::_Exit(status); }

/// A real hardware interrupt: Timer::ProcessTimers() runs in ISR context and
/// exercises dmq::os::BareMetalCriticalSection (PRIMASK) as on a real nRF51.
extern "C" void TIMER0_IRQHandler(void)
{
    Reg(EVENTS_COMPARE0) = 0;
    const uint32_t flush = Reg(EVENTS_COMPARE0); // read back: the event clear must land before the handler returns
    (void)flush;
    g_ticks = g_ticks + 1;
    dmq::util::Timer::ProcessTimers();
}
