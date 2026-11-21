#ifndef FREERTOS_SEMAPHORE_H
#define FREERTOS_SEMAPHORE_H

/// @file FreeRTOSSemaphore.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief dmq::Semaphore backing for FreeRTOS: one native binary semaphore,
/// instead of the generic dmq::ConditionVariable + dmq::Mutex implementation
/// in delegate/Semaphore.h.
///
/// @details
/// Every async-wait call (DelegateAsyncWait) creates and destroys a
/// dmq::Semaphore. The generic one is two kernel objects on FreeRTOS (a
/// mutex and the condition variable's binary semaphore), both from the heap,
/// and its Wait() and Signal() lock the mutex three or four times. A binary
/// semaphore already holds the "signaled once" state the generic class
/// tracks with a flag, so on FreeRTOS one is enough. Measured on an ESP32-S3
/// (ESP-IDF, 160 MHz): creating and destroying the generic one costs 28 us;
/// a binary semaphore in static storage, 2.3 us.
///
/// The semaphore lives inside this object (xSemaphoreCreateBinaryStatic())
/// when configSUPPORT_STATIC_ALLOCATION is 1, so it takes no heap. Otherwise
/// (e.g. STM32duino FreeRTOS) it comes from the heap (xSemaphoreCreateBinary()).
///
/// Semantics match the generic class: Signal() when already signaled is a
/// no-op (binary, not counting), and Wait() consumes one pending signal.
/// Signal() is safe from an interrupt (xSemaphoreGiveFromISR()), detected
/// the same way as FreeRTOSCriticalSection.
///
/// @note Included from DelegateOpt.h's early port-include block, before
/// "namespace dmq { ... }" opens and dmq::Duration exists, so Wait() is
/// templated on the chrono duration type (a dmq::Duration deduces through it).

#include "FreeRTOSHeaders.h"
#include "FreeRTOSCriticalSection.h"
#include <chrono>
#include <cstdint>

namespace dmq::os {

    class FreeRTOSSemaphore {
    public:
        FreeRTOSSemaphore() {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
            m_sem = xSemaphoreCreateBinaryStatic(&m_buffer);
#else
            m_sem = xSemaphoreCreateBinary();
#endif
            configASSERT(m_sem != NULL);
        }

        ~FreeRTOSSemaphore() {
            if (m_sem)
                vSemaphoreDelete(m_sem);
        }

        /// Wait for Signal().
        /// @param[in] timeout How long to wait; duration::max() waits forever.
        /// A timeout shorter than one tick waits one tick, not zero.
        /// @return true if signaled, false on timeout.
        template<typename Rep, typename Period>
        bool Wait(const std::chrono::duration<Rep, Period>& timeout) {
            if (!m_sem)
                return false;
            return xSemaphoreTake(m_sem, ToTicks(timeout)) == pdTRUE;
        }

        /// Signal the semaphore (task or interrupt context).
        void Signal() {
            if (!m_sem)
                return;
#if defined(_WIN32) || defined(WIN32) || defined(__linux__)
            // Simulator ports (Windows/Linux): no hardware interrupt to detect
            xSemaphoreGive(m_sem);
#else
            if (detail::IsInsideFreeRTOSInterrupt() != pdFALSE) {
                BaseType_t higherPriorityTaskWoken = pdFALSE;
                xSemaphoreGiveFromISR(m_sem, &higherPriorityTaskWoken);
                portYIELD_FROM_ISR(higherPriorityTaskWoken);
            } else {
                xSemaphoreGive(m_sem);
            }
#endif
        }

        FreeRTOSSemaphore(const FreeRTOSSemaphore&) = delete;
        FreeRTOSSemaphore& operator=(const FreeRTOSSemaphore&) = delete;

    private:
        template<typename Rep, typename Period>
        static TickType_t ToTicks(const std::chrono::duration<Rep, Period>& timeout) {
            if (timeout == std::chrono::duration<Rep, Period>::max())
                return portMAX_DELAY;
            if (timeout <= std::chrono::duration<Rep, Period>::zero())
                return 0;
            // Saturate before converting to milliseconds, which would overflow
            // int64 for a huge finite seconds/hours value (as CmsisRtos2ToTicks()
            // does). 2,000,000 hours is more than 2^32 ticks at any tick rate of
            // 1 Hz or above, and keeps ms * tick rate within 64 bits up to 1 MHz.
            if (timeout > std::chrono::hours(2000000))
                return static_cast<TickType_t>(static_cast<uint64_t>(portMAX_DELAY) - 1u);
            // Round up, in 64 bits (pdMS_TO_TICKS() can overflow a 32-bit
            // TickType_t for long timeouts), and stay below portMAX_DELAY,
            // which means "forever"
            const uint64_t ms = static_cast<uint64_t>(
                std::chrono::ceil<std::chrono::milliseconds>(timeout).count());
            uint64_t ticks = (ms * configTICK_RATE_HZ + 999u) / 1000u;
            if (ticks == 0)
                ticks = 1;
            const uint64_t maxTicks = static_cast<uint64_t>(portMAX_DELAY) - 1u;
            return static_cast<TickType_t>(ticks < maxTicks ? ticks : maxTicks);
        }

        SemaphoreHandle_t m_sem = NULL;
#if (configSUPPORT_STATIC_ALLOCATION == 1)
        StaticSemaphore_t m_buffer;
#endif
    };

} // namespace dmq::os

#endif // FREERTOS_SEMAPHORE_H
