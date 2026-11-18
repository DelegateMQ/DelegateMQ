/// @file bsp.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// Board support each target provides in targets/<name>/bsp.cpp. main.cpp
/// (the tests, the same on every target) uses only these.

#ifndef BARE_METAL_BSP_H
#define BARE_METAL_BSP_H

/// Short description of the target, printed at startup.
const char* BspName();

/// Start a 1 ms hardware timer interrupt. Its handler must increment g_ticks
/// (the counter dmq::os::BareMetalClock reads) and call
/// dmq::util::Timer::ProcessTimers(), from interrupt context.
void BspStartTick();

/// End the program with status (0 = all tests passed). Under QEMU with
/// semihosting this ends QEMU, and QEMU's exit status is 0 only for 0.
[[noreturn]] void BspExit(int status);

#endif // BARE_METAL_BSP_H
