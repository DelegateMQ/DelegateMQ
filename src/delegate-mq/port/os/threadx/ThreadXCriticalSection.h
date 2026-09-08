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
/// It works by disabling/restoring interrupts directly (TX_INTERRUPT_SAVE_AREA
/// / TX_DISABLE / TX_RESTORE -- the same primitive ThreadXClock.h already
/// uses for the identical reason), rather than acquiring an OS mutex object.
/// Global interrupt masking is the only ThreadX primitive that is valid in
/// both a thread and an ISR.
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
            TX_INTERRUPT_SAVE_AREA
            TX_DISABLE
            m_savedPosture = interrupt_save;
        }

        void unlock() {
            TX_INTERRUPT_SAVE_AREA
            interrupt_save = m_savedPosture;
            TX_RESTORE
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
