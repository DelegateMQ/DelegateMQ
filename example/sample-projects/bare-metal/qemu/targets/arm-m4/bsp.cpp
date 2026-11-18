/// @file bsp.cpp
/// Cortex-M4 on QEMU mps2-an386: SysTick, wired into startup.c's vector table
/// as SysTick_Handler.

#include "bsp.h"
#include "../common/cortex_m_systick.h"
#include <cstdlib>

const char* BspName() { return "Cortex-M4, arm-none-eabi-gcc, QEMU mps2-an386"; }

void BspStartTick() { SysTickStart(25000000); }

// _Exit -> retarget.c's _exit -> semihosting SYS_EXIT with the status
void BspExit(int status) { std::_Exit(status); }

extern "C" void SysTick_Handler(void) { SysTickTick(); }
