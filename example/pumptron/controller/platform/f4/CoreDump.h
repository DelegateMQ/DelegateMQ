/**
 * @file CoreDump.h
 * @brief Pumptron F4 core dump: capture the active call stack on a crash,
 *        reset, and report it after reboot.
 *
 * Adapted for Cortex-M4 / GCC from CoreDumpARM
 * (https://github.com/endurodave/CoreDumpARM, MIT License, David Lafreniere).
 *
 * On any fault (software assertion, hardware exception, thread watchdog,
 * FreeRTOS hook) the crash record is written to a RAM section that the C
 * runtime does not zero (`.noinit`), and the CPU resets immediately. The
 * record survives the warm reset; after reboot CoreDumpReporter sends it to
 * the GUI, which saves it to a dump_<timestamp>.txt file.
 *
 * Only the first fault is kept: a later fault never overwrites a record that
 * has not been reported yet.
 *
 * The call stack holds return addresses of whatever was running at the time
 * of the fault (a task or an ISR), innermost first. Decode with:
 *   arm-none-eabi-addr2line -f -C -e pumptron_f4.elf <addr> ...
 * against the image whose GNU build ID matches the dump
 * (`arm-none-eabi-readelf -n pumptron_f4.elf`).
 */

#ifndef PUMPTRON_F4_CORE_DUMP_H
#define PUMPTRON_F4_CORE_DUMP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Fault sources. Same values as pumptron::CoreDumpSource (static_assert'ed
/// in CoreDump.cpp); plain constants so the C exception handlers can use them.
#define CORE_DUMP_SRC_ASSERT      0u
#define CORE_DUMP_SRC_HARD_FAULT  1u
#define CORE_DUMP_SRC_WATCHDOG    2u
#define CORE_DUMP_SRC_RTOS        3u

/// Enable the separate MemManage, BusFault and UsageFault exceptions so a
/// fault reports its specific cause instead of escalating to HardFault.
void CoreDump_EnableFaultHandlers(void);

/// Store a software fault record (if none is stored yet) and reset the CPU.
/// @param source  CORE_DUMP_SRC_*.
/// @param where   Source file, or a task/thread name; may be NULL.
/// @param line    Source line, or 0.
/// @param aux     Source-specific code (e.g. pumptron::RtosFault), or 0.
__attribute__((noreturn))
void CoreDump_StoreAndReset(uint32_t source, const char* where, uint32_t line, uint32_t aux);

/// Hardware exception entry. Called only from the naked exception handlers
/// in stm32f4xx_it.c with the stacked exception frame and EXC_RETURN.
__attribute__((noreturn))
void CoreDump_FaultEntry(uint32_t* frame, uint32_t excReturn);

/// True if the previous run stored a valid, not yet reported core dump.
bool CoreDump_IsStored(void);

/// Discard the stored record (after it has been delivered).
void CoreDump_Clear(void);

#ifdef __cplusplus
}

namespace pumptron { struct CoreDumpMsg; }

/// Copy the stored record into a message. @return false if none is stored.
bool CoreDump_ToMsg(pumptron::CoreDumpMsg& msg);
#endif

#endif
