/// @file bsp.cpp
/// Cortex-M33 on QEMU mps2-an505: SysTick (the Secure one: the core runs
/// Secure from reset), wired into ../arm-m4/startup.c's vector table as
/// SysTick_Handler.

#include "bsp.h"
#include "../common/cortex_m_systick.h"
#include <cstdlib>

const char* BspName() { return "Cortex-M33 (Armv8-M), arm-none-eabi-gcc, QEMU mps2-an505"; }

// QEMU's mps2-an505 clocks the core at 20 MHz, as Arm's AN505 does
void BspStartTick() { SysTickStart(20000000); }

// _Exit -> retarget.c's _exit -> semihosting SYS_EXIT with the status
void BspExit(int status) { std::_Exit(status); }

extern "C" void SysTick_Handler(void) { SysTickTick(); }
