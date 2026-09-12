#ifndef CMSIS_COMPILER_STUB_H
#define CMSIS_COMPILER_STUB_H

/// @file cmsis_compiler.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// *** SAMPLE-LOCAL VERIFICATION STUB -- NOT PART OF THE DELEGATEMQ LIBRARY ***
///
/// The real cmsis_compiler.h (ARM CMSIS-Core) declares __get_PRIMASK()/
/// __disable_irq()/__set_PRIMASK() as inline ARM assembly (e.g. `cpsid i`,
/// `mrs %0, PRIMASK`) -- instructions that exist only on ARM Cortex-M and
/// cannot be assembled for native_sim's x86-64 host process. There is no
/// real CMSIS-Core package on this include path for that reason.
///
/// dmq::os::CmsisRtos2CriticalSection (port/os/cmsis-rtos2/CmsisRtos2CriticalSection.h)
/// calls these three functions, and DelegateMQ's Timer.cpp needs a working
/// dmq::CriticalSection to build at all under DMQ_THREAD_CMSIS_RTOS2 -- so
/// without *something* providing these symbols, nothing in this sample
/// compiles, including the Thread/Mutex/Semaphore/DelegateQueue code this
/// sample exists to verify.
///
/// This stub provides NO-OP, single-core-host implementations purely to
/// unblock that build. They do NOT mask interrupts (there is no ISR concept
/// reachable from a native_sim userspace process to begin with) and do NOT
/// exercise or validate CmsisRtos2CriticalSection's real ISR-safety
/// behavior in any way -- that still requires real ARM Cortex-M hardware or
/// QEMU + arm-none-eabi-gcc/ATfE with the genuine CMSIS-Core headers. See
/// CLAUDE.md's "ISR-Safe Locking" section: CmsisRtos2CriticalSection remains
/// listed there as unverified, and this stub does not change that.
///
/// This header is intentionally NOT under src/delegate-mq/ -- it is scoped
/// to this one sample's include path (see CMakeLists.txt) so it can never be
/// picked up by a real embedded build, where a vendor-supplied CMSIS-Core
/// package must be used instead.

#include <cstdint>

static inline uint32_t __get_PRIMASK(void) { return 0; }
static inline void __set_PRIMASK(uint32_t priMask) { (void)priMask; }
static inline void __disable_irq(void) {}

#endif // CMSIS_COMPILER_STUB_H
