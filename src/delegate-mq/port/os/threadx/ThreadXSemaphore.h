#ifndef THREADX_SEMAPHORE_H
#define THREADX_SEMAPHORE_H

/// @file ThreadXSemaphore.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief dmq::Semaphore backing for ThreadX: one native TX_SEMAPHORE,
/// instead of the generic dmq::ConditionVariable + dmq::Mutex implementation
/// in delegate/Semaphore.h.
///
/// @details
/// Every async-wait call (DelegateAsyncWait) creates and destroys a
/// dmq::Semaphore. The generic one is two kernel objects on ThreadX (a
/// TX_MUTEX and the condition variable's TX_SEMAPHORE), and its Wait() and
/// Signal() lock the mutex three or four times. One TX_SEMAPHORE already
/// holds the "signaled" state the generic class tracks with a flag. Like all
/// ThreadX objects its control block lives in this object: no heap.
///
/// Semantics match the generic class: binary, not counting. Signal() uses
/// tx_semaphore_ceiling_put() with a ceiling of 1, so a second Signal()
/// before a Wait() is a no-op; Wait() consumes the one pending signal. Both
/// tx_semaphore_ceiling_put() and tx_semaphore_get(TX_NO_WAIT) are callable
/// from an interrupt.
///
/// @note Included from DelegateOpt.h's early port-include block, before
/// "namespace dmq { ... }" opens and dmq::Duration exists, so Wait() is
/// templated on the chrono duration type (a dmq::Duration deduces through it).

#include "tx_api.h"
#include <chrono>
#include <cstdint>

namespace dmq::os {

    class ThreadXSemaphore {
    public:
        ThreadXSemaphore() {
            // (CHAR*): the C API takes a non-const name
            const UINT status = tx_semaphore_create(&m_sem, (CHAR*)"DMQ_Semaphore", 0);
            DMQ_ASSERT_TRUE(status == TX_SUCCESS);
        }

        ~ThreadXSemaphore() {
            tx_semaphore_delete(&m_sem);
        }

        /// Wait for Signal().
        /// @param[in] timeout How long to wait; duration::max() waits forever.
        /// A timeout shorter than one tick waits one tick, not zero.
        /// @return true if signaled, false on timeout.
        template<typename Rep, typename Period>
        bool Wait(const std::chrono::duration<Rep, Period>& timeout) {
            return tx_semaphore_get(&m_sem, ToTicks(timeout)) == TX_SUCCESS;
        }

        /// Signal the semaphore (thread or interrupt context). Already
        /// signaled: no change (TX_CEILING_EXCEEDED, ignored).
        void Signal() {
            tx_semaphore_ceiling_put(&m_sem, 1);
        }

        ThreadXSemaphore(const ThreadXSemaphore&) = delete;
        ThreadXSemaphore& operator=(const ThreadXSemaphore&) = delete;

    private:
        template<typename Rep, typename Period>
        static ULONG ToTicks(const std::chrono::duration<Rep, Period>& timeout) {
            if (timeout == std::chrono::duration<Rep, Period>::max())
                return TX_WAIT_FOREVER;
            if (timeout <= std::chrono::duration<Rep, Period>::zero())
                return TX_NO_WAIT;
            // Saturate before converting to milliseconds, which would overflow
            // int64 for a huge finite seconds/hours value (as CmsisRtos2ToTicks()
            // does). 2,000,000 hours is more than 2^32 ticks at any tick rate of
            // 1 Hz or above, and keeps ms * tick rate within 64 bits up to 1 MHz.
            if (timeout > std::chrono::hours(2000000))
                return static_cast<ULONG>(static_cast<uint64_t>(TX_WAIT_FOREVER) - 1u);
            // Round up, in 64 bits, and stay below TX_WAIT_FOREVER
            const uint64_t ms = static_cast<uint64_t>(
                std::chrono::ceil<std::chrono::milliseconds>(timeout).count());
            uint64_t ticks = (ms * TX_TIMER_TICKS_PER_SECOND + 999u) / 1000u;
            if (ticks == 0)
                ticks = 1;
            const uint64_t maxTicks = static_cast<uint64_t>(TX_WAIT_FOREVER) - 1u;
            return static_cast<ULONG>(ticks < maxTicks ? ticks : maxTicks);
        }

        TX_SEMAPHORE m_sem;
    };

} // namespace dmq::os

#endif // THREADX_SEMAPHORE_H
