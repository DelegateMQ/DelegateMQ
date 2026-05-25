#ifndef FREERTOS_CONDITION_VARIABLE_H
#define FREERTOS_CONDITION_VARIABLE_H

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h" 
#include <chrono>

namespace dmq::os {
    /// @brief Production-grade wrapper around FreeRTOS Semaphore to mimic std::condition_variable
    /// @details 
    /// - Uses a Binary Semaphore (1 token).
    /// - ISR-safe notification logic.
    /// - Robust tick overflow handling using elapsed time subtraction.
    /// 
    /// @note Limitation: Unlike std::cv, a binary semaphore retains its signal state.
    /// If notify() occurs before wait(), the wait will effectively "fall through". 
    /// Multiple notifies before a wait are coalesced into a single signal.
    class FreeRTOSConditionVariable
    {
    public:
        FreeRTOSConditionVariable() 
        { 
            // Binary semaphore is sufficient for signaling state changes
            m_sem = xSemaphoreCreateBinary(); 
            
            // Critical check: Ensure heap was sufficient
            configASSERT(m_sem != NULL);
        }

        ~FreeRTOSConditionVariable() 
        { 
            if (m_sem) {
                vSemaphoreDelete(m_sem);
            }
        }

        FreeRTOSConditionVariable(const FreeRTOSConditionVariable&) = delete;
        FreeRTOSConditionVariable& operator=(const FreeRTOSConditionVariable&) = delete;

        /// @brief Wake up one waiting thread (ISR Safe)
        void notify_one() noexcept
        {
            if (!m_sem) return;

#if defined(_WIN32) || defined(WIN32) || defined(__linux__)
            // Simulator Ports (Windows/Linux): 
            // "Interrupts" are simulated. The hardware ISR check 
            // often does not exist. Standard API is safe here.
            xSemaphoreGive(m_sem);
#else
            // Embedded (e.g., ARM Cortex-M): 
            // Must check if running in ISR context to use FromISR API.
            if (xPortIsInsideInterrupt())
            {
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                xSemaphoreGiveFromISR(m_sem, &xHigherPriorityTaskWoken);
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            }
            else
            {
                xSemaphoreGive(m_sem);
            }
#endif
        }

        /// @brief Wait indefinitely until predicate is true
        template <typename Lock, typename Predicate>
        void wait(Lock& lock, Predicate pred)
        {
            while (!pred())
            {
                if (lock.owns_lock())
                    lock.unlock();

                // Wait indefinitely
                BaseType_t ret = xSemaphoreTake(m_sem, portMAX_DELAY);
                configASSERT(ret == pdTRUE);

                if (!lock.owns_lock())
                    lock.lock();
            }
        }

        /// @brief Wait until predicate is true or timeout expires
        /// @details Uses overflow-safe subtraction logic to track elapsed ticks.
        template <typename Lock, typename Predicate>
        bool wait_for(Lock& lock, std::chrono::milliseconds timeout, Predicate pred)
        {
            const TickType_t timeoutTicks = pdMS_TO_TICKS(timeout.count());
            const TickType_t start = xTaskGetTickCount();

            while (!pred())
            {
                TickType_t now = xTaskGetTickCount();
                
                // Overflow-safe elapsed time calculation
                TickType_t elapsed = now - start;

                if (elapsed >= timeoutTicks)
                {
                    if (!lock.owns_lock())
                        lock.lock();
                    return pred(); // Timeout expired, check predicate one last time
                }

                TickType_t remaining = timeoutTicks - elapsed;

                if (lock.owns_lock())
                    lock.unlock();
                
                // Wait for the semaphore or timeout
                BaseType_t res = xSemaphoreTake(m_sem, remaining);
                
                if (!lock.owns_lock())
                    lock.lock();

                if (res == pdFALSE)
                {
                    // Semaphore timeout occurred
                    return pred(); 
                }
                
                // If success (res == pdTRUE), loop around and check pred() again.
                // If pred() is still false, it was a spurious wakeup or coalesced signal.
            }

            return true;
        }

    private:
        SemaphoreHandle_t m_sem = NULL;
    };
} // namespace dmq::os

#endif // FREERTOS_CONDITION_VARIABLE_H