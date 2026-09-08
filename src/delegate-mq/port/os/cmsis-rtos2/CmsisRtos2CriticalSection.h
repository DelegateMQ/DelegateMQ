#ifndef CMSIS_RTOS2_CRITICAL_SECTION_H
#define CMSIS_RTOS2_CRITICAL_SECTION_H

/// @file CmsisRtos2CriticalSection.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief ISR-safe critical section for CMSIS-RTOS2.
///
/// @details
/// osMutexAcquire()/osMutexNew() are explicitly documented as not callable
/// from Interrupt Service Routines. Unlike Zephyr/ThreadX, CMSIS-RTOS2 has
/// no ISR-safe primitive of its own for this -- so this type bypasses
/// osMutex entirely and drops to the ARM Cortex-M CMSIS-Core intrinsics
/// directly (__get_PRIMASK()/__disable_irq()/__set_PRIMASK()), since
/// CMSIS-RTOS2 always sits on ARM Cortex-M. This is the same save/restore
/// technique BareMetalClock.h and ThreadXCriticalSection.h already use for
/// the identical reason: global interrupt masking is the only primitive
/// that is valid from both thread and ISR context on this architecture.
///
/// @note UNVERIFIED: written against documented CMSIS-RTOS2/CMSIS-Core API
/// behavior. No CMSIS-RTOS2 SDK (e.g. Keil RTX5) or Cortex-M hardware/QEMU
/// target is available in this development environment to build and run it
/// (unlike ThreadXCriticalSection, exercised for real via the threadx-linux
/// sample). __get_PRIMASK()/__set_PRIMASK()/__disable_irq() are standard
/// CMSIS-Core intrinsics, declared in the target's core_cmX.h (e.g.
/// core_cm4.h) and normally reachable transitively through cmsis_os2.h in
/// a real CMSIS-RTOS2 project -- confirm that on the actual target before
/// relying on this.
///
/// *** NARROW PURPOSE -- DO NOT USE THIS AS A GENERAL-PURPOSE LOCK ***
/// Holding this masks ALL maskable interrupts on the CPU for as long as it
/// is held. Only use it to protect something genuinely tiny and bounded
/// that may be touched from ISR context. Do NOT use it in place of
/// dmq::Mutex / dmq::RecursiveMutex for ordinary thread-to-thread
/// synchronization (e.g. DataBus internals, a Thread's message queue) --
/// anything that can block, take a while, or run for an unbounded time
/// must not run with interrupts globally masked; that is a real-time
/// correctness bug on hardware, not just a style issue.
///
/// *** NOT RE-LOCKABLE ON THE SAME INSTANCE ***
/// Unlike dmq::RecursiveMutex, calling lock() twice on the SAME instance
/// without an intervening unlock() is incorrect: the second lock() call
/// overwrites the single saved PRIMASK member with the (already disabled)
/// inner state, so the outer unlock() restores the wrong state and
/// interrupts are left disabled permanently. There is no "recursive"
/// variant of this type -- it has no ownership concept to make recursion
/// meaningful, and Timer's own usage never nests. Locking two DIFFERENT
/// instances in proper LIFO lock/unlock order is fine: each instance saves
/// and restores its own PRIMASK independently.

#include "cmsis_os2.h"

namespace dmq::os {

    // =========================================================================
    // CmsisRtos2CriticalSection
    // See the file-level comment above before using this type anywhere new.
    // =========================================================================
    class CmsisRtos2CriticalSection {
    public:
        CmsisRtos2CriticalSection() = default;

        void lock() {
            m_savedPrimask = __get_PRIMASK();
            __disable_irq();
        }

        void unlock() {
            __set_PRIMASK(m_savedPrimask);
        }

        // No try_lock(): interrupt masking cannot fail to "acquire", so a
        // try_lock() here would always trivially succeed and isn't meaningful.

        CmsisRtos2CriticalSection(const CmsisRtos2CriticalSection&) = delete;
        CmsisRtos2CriticalSection& operator=(const CmsisRtos2CriticalSection&) = delete;

    private:
        uint32_t m_savedPrimask = 0;
    };

} // namespace dmq::os

#endif // CMSIS_RTOS2_CRITICAL_SECTION_H
