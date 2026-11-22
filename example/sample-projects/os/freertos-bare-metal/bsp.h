/// @file bsp.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// Board support for the Cortex-M targets (targets/<name>/, run on QEMU),
/// defined by them with BSP_TARGET. The Win32 simulator build has none of
/// this: it has no hardware interrupts, and its tests never exit.

#ifndef FREERTOS_BARE_METAL_BSP_H
#define FREERTOS_BARE_METAL_BSP_H

#if defined(BSP_TARGET)

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Short description of the target, printed at startup.
const char* BspName(void);

/// Start a hardware timer interrupt at hz, calling handler from the
/// interrupt at a priority that may use the FreeRTOS FromISR API.
void BspStartTimerIsr(uint32_t hz, void (*handler)(void));

/// Stop the timer interrupt.
void BspStopTimerIsr(void);

/// End the program with status (0 = all tests passed). Under QEMU with
/// semihosting this ends QEMU with the same status.
void BspExit(int status) __attribute__((noreturn));

/// Calls to malloc, calloc and realloc (C++ new included) since startup.
uint32_t BspHeapCallCount(void);

/// The return address and size of the last heap call, to find what allocated.
void* BspHeapLastCaller(size_t* size);

/// Set by a test that expects a watchdog expiry (Test 15): the target's
/// WatchdogHandler() calls it instead of failing the run. It checks the
/// result and ends the run itself.
extern void (*BspWatchdogExpected)(const char* threadName);

#ifdef __cplusplus
}
#endif

#endif // BSP_TARGET

#endif // FREERTOS_BARE_METAL_BSP_H
