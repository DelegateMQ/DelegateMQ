#ifndef ZEPHYR_CRITICAL_SECTION_H
#define ZEPHYR_CRITICAL_SECTION_H

/// @file ZephyrCriticalSection.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief ISR-safe critical section for Zephyr.
///
/// @details
/// k_mutex_lock() is a priority-inheritance-aware, thread-only primitive in
/// Zephyr, like every other RTOS here -- it cannot be used from ISR context.
/// This type uses irq_lock()/irq_unlock(key) instead, which Zephyr documents
/// as safe from both thread and ISR context (it's Zephyr's own primitive for
/// exactly this: protecting something tiny that ISR and thread code both
/// touch). Unlike ThreadX's plain disable/restore, irq_lock() returns a key
/// that must be threaded back into irq_unlock() -- stored here as a member so
/// it survives the gap between the separate lock()/unlock() calls.
///
/// @note UNVERIFIED: written against documented Zephyr kernel API behavior.
/// No Zephyr SDK/west workspace is available in this development environment
/// to build and run it (unlike ThreadXCriticalSection, exercised for real via
/// the threadx-linux sample). Review carefully, and exercise on real Zephyr
/// hardware or a west-based simulation (e.g. native_sim) before relying on it.
///
/// *** NARROW PURPOSE -- DO NOT USE THIS AS A GENERAL-PURPOSE LOCK ***
/// Holding this masks interrupts on the CPU for as long as it is held. Only
/// use it to protect something genuinely tiny and bounded that may be
/// touched from ISR context. Do NOT use it in place of dmq::Mutex /
/// dmq::RecursiveMutex for ordinary thread-to-thread synchronization (e.g.
/// DataBus internals, a Thread's message queue) -- anything that can block,
/// take a while, or run for an unbounded time must not run with interrupts
/// globally masked; that is a real-time correctness bug on hardware, not
/// just a style issue.
///
/// *** NOT RE-LOCKABLE ON THE SAME INSTANCE ***
/// Unlike dmq::RecursiveMutex, calling lock() twice on the SAME instance
/// without an intervening unlock() is incorrect: the second lock() call
/// overwrites the single saved key with the (already locked) inner key, so
/// the outer unlock() restores the wrong state. There is no "recursive"
/// variant of this type -- it has no ownership concept to make recursion
/// meaningful, and Timer's own usage never nests. Locking two DIFFERENT
/// instances in proper LIFO lock/unlock order is fine: each instance saves
/// and restores its own key independently.

#include <zephyr/kernel.h>

namespace dmq::os {

    // =========================================================================
    // ZephyrCriticalSection
    // See the file-level comment above before using this type anywhere new.
    // =========================================================================
    class ZephyrCriticalSection {
    public:
        ZephyrCriticalSection() = default;

        void lock() {
            m_key = irq_lock();
        }

        void unlock() {
            irq_unlock(m_key);
        }

        // No try_lock(): interrupt masking cannot fail to "acquire", so a
        // try_lock() here would always trivially succeed and isn't meaningful.

        ZephyrCriticalSection(const ZephyrCriticalSection&) = delete;
        ZephyrCriticalSection& operator=(const ZephyrCriticalSection&) = delete;

    private:
        unsigned int m_key = 0;
    };

} // namespace dmq::os

#endif // ZEPHYR_CRITICAL_SECTION_H
