/// @file bsp.cpp
/// Cortex-M3 on QEMU mps2-an385, built with ATfE: SysTick. ATfE's vector
/// table (picolibc's __interrupt_vector) calls the weak arm_systick_isr for
/// SysTick, so defining it here installs the handler.

#include "bsp.h"
#include "../common/cortex_m_systick.h"
#include <cstdlib>

const char* BspName() { return "Cortex-M3, ATfE (clang, picolibc, libc++), QEMU mps2-an385"; }

void BspStartTick() { SysTickStart(25000000); }

// _Exit -> ATfE libsemihost's _exit -> semihosting exit with the status
void BspExit(int status) { std::_Exit(status); }

extern "C" void arm_systick_isr(void) { SysTickTick(); }
