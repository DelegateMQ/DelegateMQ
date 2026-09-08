#ifndef THREADX_CRITICAL_SECTION_H
#define THREADX_CRITICAL_SECTION_H

/// @file ThreadXCriticalSection.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief ISR-safe critical section for ThreadX.
///
/// @details
/// ThreadX's TX_MUTEX cannot be used from a genuine hardware ISR: both
/// tx_mutex_create() and tx_mutex_get() check TX_THREAD_GET_SYSTEM_STATE()
/// and return TX_CALLER_ERROR when called from interrupt context (verified
/// directly against common/src/txe_mutex_create.c and txe_mutex_get.c).
/// This type exists for the narrow set of call sites that must work from
/// BOTH thread and ISR context -- currently dmq::util::Timer's internal
/// list lock, since dmq::util::Timer::ProcessTimers() is documented as
/// callable from the highest-priority context available, including a
/// hardware ISR.
///
/// It works by disabling/restoring interrupts directly, the same underlying
/// mechanism ThreadXClock.h uses via TX_INTERRUPT_SAVE_AREA / TX_DISABLE /
/// TX_RESTORE. This class can't use those macros themselves, though: they
/// declare a local save-area variable whose NAME is port-specific (e.g.
/// "interrupt_save" on most ARM ports, "tx_saved_posture" on Linux/GNU) and
/// only valid within the function that declared it -- fine for ThreadXClock.h,
/// where disable and restore happen in the same function, but lock() and
/// unlock() here are separate calls, so the saved posture must survive in a
/// member variable across the gap. Instead this uses tx_interrupt_control(),
/// the portable, publicly documented ThreadX service for exactly this: it
/// takes the new posture (TX_INT_DISABLE) and returns the previous one to
/// restore later, with no port-specific local variable involved. It's the
/// same interrupt-lockout primitive TX_DISABLE/TX_RESTORE use internally, so
/// it carries the same ISR-safety guarantee.
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
/// overwrites the single saved-interrupt-state member with the (already
/// disabled) inner posture, so the outer unlock() restores the wrong
/// state and interrupts are left disabled permanently. There is no
/// "recursive" variant of this type -- it has no ownership concept to
/// make recursion meaningful, and Timer's own usage never nests. Locking
/// two DIFFERENT instances in proper LIFO lock/unlock order is fine: each
/// instance saves and restores its own interrupt state independently.

#include "tx_api.h"

namespace dmq::os {

    // =========================================================================
    // ThreadXCriticalSection
    // See the file-level comment above before using this type anywhere new.
    // =========================================================================
    class ThreadXCriticalSection {
    public:
        ThreadXCriticalSection() = default;

        void lock() {
            m_savedPosture = tx_interrupt_control(TX_INT_DISABLE);
        }

        void unlock() {
            tx_interrupt_control(m_savedPosture);
        }

        // No try_lock(): interrupt masking cannot fail to "acquire", so a
        // try_lock() here would always trivially succeed and isn't meaningful.

        ThreadXCriticalSection(const ThreadXCriticalSection&) = delete;
        ThreadXCriticalSection& operator=(const ThreadXCriticalSection&) = delete;

    private:
        UINT m_savedPosture = 0;
    };

} // namespace dmq::os

#endif // THREADX_CRITICAL_SECTION_H
