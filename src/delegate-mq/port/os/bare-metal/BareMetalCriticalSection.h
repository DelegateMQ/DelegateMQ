#ifndef BARE_METAL_CRITICAL_SECTION_H
#define BARE_METAL_CRITICAL_SECTION_H

/// @file BareMetalCriticalSection.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief ISR-safe critical section for bare metal (no RTOS).
///
/// @details
/// Bare metal has no threads, so dmq::Mutex/RecursiveMutex are correctly a
/// no-op (NullMutex) there -- but that same no-op is NOT correct for a
/// critical section: real hardware ISRs still exist and can still preempt
/// the main loop even with no RTOS at all. dmq::util::Timer::ProcessTimers()
/// is commonly driven from a hardware ISR on this port (e.g.
/// SysTick_Handler), so its lock genuinely needs interrupt masking.
///
/// Uses the same save-and-disable/restore technique, and the same per-
/// architecture branching, that BareMetalClock.h already uses for the
/// identical reason -- raw inline assembly (ARM: MRS/MSR PRIMASK: RISC-V:
/// CSRRCI/CSRS on mstatus.MIE) rather than a vendor intrinsics header,
/// since a minimal bare-metal build may not pull in the full CMSIS-Core
/// headers CmsisRtos2CriticalSection.h relies on for ARM, and there is no
/// RISC-V equivalent of CMSIS-Core at all.
///
/// *** NARROW PURPOSE -- DO NOT USE THIS AS A GENERAL-PURPOSE LOCK ***
/// Holding this masks ALL maskable interrupts on the CPU for as long as it
/// is held. Only use it to protect something genuinely tiny and bounded
/// that may be touched from ISR context.
///
/// *** NOT RE-LOCKABLE ON THE SAME INSTANCE ***
/// Calling lock() twice on the SAME instance without an intervening
/// unlock() is incorrect -- see ThreadXCriticalSection.h for the full
/// explanation; the same reasoning applies here. Locking two DIFFERENT
/// instances in proper LIFO lock/unlock order is fine.

#include <cstdint>

namespace dmq::os {

    // =========================================================================
    // BareMetalCriticalSection
    // See the file-level comment above before using this type anywhere new.
    // =========================================================================
    class BareMetalCriticalSection {
    public:
        BareMetalCriticalSection() = default;

        void lock() {
#if defined(__riscv)
            // csrrci atomically reads mstatus into m_savedState AND clears
            // the bits set in the immediate (0x8 = MIE, bit 3) in one
            // instruction -- no separate disable step needed.
            __asm__ volatile ("csrrci %0, mstatus, 0x8" : "=r" (m_savedState));
#elif defined(__arm__) && (defined(__GNUC__) || defined(__clang__))
            __asm__ volatile ("mrs %0, primask" : "=r" (m_savedState));
            __asm__ volatile ("cpsid i" ::: "memory");
#elif defined(__CC_ARM) || defined(__ARMCC_VERSION)
            m_savedState = __get_PRIMASK();
            __disable_irq();
#else
            // Fallback: no real interrupt masking available on this
            // toolchain/target -- see BareMetalClock.h for the same
            // fallback rationale.
            m_savedState = 0;
#endif
        }

        void unlock() {
#if defined(__riscv)
            // Restore mstatus.MIE only if it was set before lock() --
            // csrs ORs its argument's set bits into mstatus, so masking
            // m_savedState down to just the MIE bit before restoring
            // means an already-clear MIE (interrupts were off before this
            // lock() at all, e.g. nested inside another disabled region)
            // stays clear rather than being unconditionally forced back on.
            __asm__ volatile ("csrs mstatus, %0" :: "r" (m_savedState & 0x8u) : "memory");
#elif defined(__arm__) && (defined(__GNUC__) || defined(__clang__))
            __asm__ volatile ("msr primask, %0" :: "r" (m_savedState) : "memory");
#elif defined(__CC_ARM) || defined(__ARMCC_VERSION)
            __set_PRIMASK(m_savedState);
#endif
        }

        // No try_lock(): interrupt masking cannot fail to "acquire", so a
        // try_lock() here would always trivially succeed and isn't meaningful.

        BareMetalCriticalSection(const BareMetalCriticalSection&) = delete;
        BareMetalCriticalSection& operator=(const BareMetalCriticalSection&) = delete;

    private:
        uint32_t m_savedState = 0;
    };

} // namespace dmq::os

#endif // BARE_METAL_CRITICAL_SECTION_H
