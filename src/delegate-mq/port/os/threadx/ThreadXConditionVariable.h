#ifndef THREADX_CONDITION_VARIABLE_H
#define THREADX_CONDITION_VARIABLE_H

#include "tx_api.h"
#include <chrono>

namespace dmq::os {
    /// @brief Production-grade wrapper around ThreadX Semaphore to mimic std::condition_variable
    /// @details 
    /// - Uses a Counting Semaphore (initialized to 0).
    /// - ISR-safe notification logic (tx_semaphore_put).
    /// - Robust tick overflow handling using elapsed time subtraction.
    /// 
    /// @note Limitation: Unlike std::cv, a semaphore retains its signal state.
    /// If notify() occurs before wait(), the wait will effectively "fall through". 
    class ThreadXConditionVariable
    {
    public:
        ThreadXConditionVariable()
        {
            // Create a semaphore with initial count 0.
            // Cast string literal to (CHAR*) to satisfy strict C++ compilers interfacing with C API.
            UINT status = tx_semaphore_create(&m_sem, (CHAR*)"DMQ_CondVar", 0);
            DMQ_ASSERT_TRUE(status == TX_SUCCESS);
        }

        ~ThreadXConditionVariable()
        {
            tx_semaphore_delete(&m_sem);
        }

        ThreadXConditionVariable(const ThreadXConditionVariable&) = delete;
        ThreadXConditionVariable& operator=(const ThreadXConditionVariable&) = delete;

        /// @brief Wake up one waiting thread (ISR Safe)
        void notify_one() noexcept
        {
            // ThreadX tx_semaphore_put is ISR-safe.
            // It increments the semaphore count.
            tx_semaphore_put(&m_sem);
        }

        /// @brief Wait indefinitely until predicate is true
        template <typename Lock, typename Predicate>
        void wait(Lock& lock, Predicate pred)
        {
            while (!pred())
            {
                lock.unlock();
                // Wait indefinitely
                tx_semaphore_get(&m_sem, TX_WAIT_FOREVER);
                lock.lock();
            }
        }

        /// @brief Wait until predicate is true or timeout expires
        template <typename Lock, typename Predicate>
        bool wait_for(Lock& lock, std::chrono::milliseconds timeout, Predicate pred)
        {
            // 1. Convert timeout to ticks safely
            // Use unsigned long long to prevent overflow during multiplication (ms * ticks_per_sec)
            const ULONG ticksPerSec = TX_TIMER_TICKS_PER_SECOND;
            unsigned long long totalTicks = (static_cast<unsigned long long>(timeout.count()) * ticksPerSec) / 1000ULL;

            // Ensure at least 1 tick if timeout > 0 to avoid immediate timeout
            if (totalTicks == 0 && timeout.count() > 0) totalTicks = 1;

            // Cap at ThreadX max wait if necessary, though unlikely for ms ranges
            const ULONG timeoutTicks = (totalTicks > 0xFFFFFFFF) ? 0xFFFFFFFF : (ULONG)totalTicks;

            const ULONG startTick = tx_time_get();

            while (!pred())
            {
                ULONG now = tx_time_get();

                // Overflow-safe subtraction (works because ULONG is unsigned)
                ULONG elapsed = now - startTick;

                if (elapsed >= timeoutTicks)
                    return pred(); // Timeout expired

                ULONG remaining = timeoutTicks - elapsed;

                lock.unlock();

                // Wait for the semaphore or timeout
                UINT res = tx_semaphore_get(&m_sem, remaining);

                lock.lock();

                if (res != TX_SUCCESS)
                {
                    // TX_NO_INSTANCE (0x0D) or TX_WAIT_ABORTED means we didn't get the token.
                    // Timeout occurred or wait aborted.
                    return pred();
                }

                // If res == TX_SUCCESS, we consumed a token. 
                // Loop around and check pred() again. 
                // If pred() is false, we consumed a signal intended for state that isn't ready,
                // effectively acting as a "spurious wakeup" handler.
            }

            return true;
        }

    private:
        TX_SEMAPHORE m_sem;
    };
} // namespace dmq::os

#endif // THREADX_CONDITION_VARIABLE_H