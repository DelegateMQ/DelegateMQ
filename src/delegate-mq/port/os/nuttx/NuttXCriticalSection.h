#ifndef NUTTX_CRITICAL_SECTION_H
#define NUTTX_CRITICAL_SECTION_H

/// @file NuttXCriticalSection.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief ISR-safe critical section for NuttX.
///
/// @details
/// pthread_mutex_lock() is a priority-inheritance-aware, thread-only
/// primitive on NuttX, like every other RTOS mutex in this library -- it
/// cannot be used from ISR context. This type uses up_irq_save()/
/// up_irq_restore(flags) instead: NuttX's own architecture-portable
/// interrupt-masking primitive (declared in <nuttx/irq.h>), implemented on
/// every supported CPU architecture and used throughout NuttX's own kernel
/// for exactly this purpose -- protecting something tiny that both ISR and
/// thread code touch. This is the same save/disable/restore technique
/// ThreadXCriticalSection and ZephyrCriticalSection use, just NuttX's own
/// primitive instead of a vendor CMSIS-Core intrinsic.
///
/// @warning up_irq_save()/up_irq_restore() are only reachable from
/// application code in a NuttX FLAT build (the default for most boards,
/// where apps and the kernel share one address space). In a PROTECTED or
/// KERNEL build, application code runs in unprivileged userspace with no
/// access to this call, and dmq::util::Timer::ProcessTimers() must instead
/// be driven from kernel-side code (e.g. a NuttX interrupt handler or a
/// kernel-mode work queue) on those configurations.
///
/// @note UNVERIFIED: written against documented NuttX architecture-layer
/// API behavior. No NuttX toolchain/simulator is available in this
/// development environment to build and run it (unlike ThreadXCriticalSection,
/// exercised for real via the threadx-linux sample). Review carefully, and
/// exercise on real NuttX hardware or `nuttx/boards/sim` before relying on
/// it in production.
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
/// overwrites the single saved irqstate_t with the (already masked) inner
/// state, so the outer unlock() restores the wrong state. There is no
/// "recursive" variant of this type -- it has no ownership concept to make
/// recursion meaningful, and Timer's own usage never nests. Locking two
/// DIFFERENT instances in proper LIFO lock/unlock order is fine: each
/// instance saves and restores its own state independently.

#include <nuttx/irq.h>

namespace dmq::os {

    // =========================================================================
    // NuttXCriticalSection
    // See the file-level comment above before using this type anywhere new.
    // =========================================================================
    class NuttXCriticalSection {
    public:
        NuttXCriticalSection() = default;

        void lock() {
            m_flags = up_irq_save();
        }

        void unlock() {
            up_irq_restore(m_flags);
        }

        // No try_lock(): interrupt masking cannot fail to "acquire", so a
        // try_lock() here would always trivially succeed and isn't meaningful.

        NuttXCriticalSection(const NuttXCriticalSection&) = delete;
        NuttXCriticalSection& operator=(const NuttXCriticalSection&) = delete;

    private:
        irqstate_t m_flags = 0;
    };

} // namespace dmq::os

#endif // NUTTX_CRITICAL_SECTION_H
