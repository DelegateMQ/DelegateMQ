#ifndef FREERTOS_MUTEX_H
#define FREERTOS_MUTEX_H

#include "FreeRTOSHeaders.h"

namespace dmq::os {

    // =========================================================================
    // FreeRTOSMutex (Non-Recursive)
    // Matches std::mutex behavior
    // =========================================================================
    class FreeRTOSMutex {
    public:
        FreeRTOSMutex() {
            m_handle = xSemaphoreCreateMutex();
            configASSERT(m_handle);
        }

        ~FreeRTOSMutex() {
            if (m_handle) {
                vSemaphoreDelete(m_handle);
            }
        }

        void lock() {
            if (m_handle) {
                BaseType_t ret = xSemaphoreTake(m_handle, portMAX_DELAY);
                configASSERT(ret == pdTRUE);
            }
        }

        bool try_lock() {
            if (m_handle) {
                // Wait time of 0 ticks = non-blocking attempt
                return xSemaphoreTake(m_handle, 0) == pdTRUE;
            }
            return false;
        }

        void unlock() {
            if (m_handle) {
                xSemaphoreGive(m_handle);
            }
        }

        FreeRTOSMutex(const FreeRTOSMutex&) = delete;
        FreeRTOSMutex& operator=(const FreeRTOSMutex&) = delete;

    private:
        SemaphoreHandle_t m_handle = nullptr;
    };

    // =========================================================================
    // FreeRTOSRecursiveMutex (Recursive)
    // Matches std::recursive_mutex behavior
    // =========================================================================
    class FreeRTOSRecursiveMutex {
    public:
        FreeRTOSRecursiveMutex() {
            // Requires configUSE_RECURSIVE_MUTEXES == 1 in FreeRTOSConfig.h
            m_handle = xSemaphoreCreateRecursiveMutex();
            configASSERT(m_handle);
        }

        ~FreeRTOSRecursiveMutex() {
            if (m_handle) {
                vSemaphoreDelete(m_handle);
            }
        }

        void lock() {
            if (m_handle) {
                BaseType_t ret = xSemaphoreTakeRecursive(m_handle, portMAX_DELAY);
                configASSERT(ret == pdTRUE);
            }
        }

        // Added try_lock (Required by std::unique_lock)
        bool try_lock() {
            if (m_handle) {
                // Wait time of 0 ticks = non-blocking attempt
                return xSemaphoreTakeRecursive(m_handle, 0) == pdTRUE;
            }
            return false;
        }

        void unlock() {
            if (m_handle) {
                xSemaphoreGiveRecursive(m_handle);
            }
        }

        FreeRTOSRecursiveMutex(const FreeRTOSRecursiveMutex&) = delete;
        FreeRTOSRecursiveMutex& operator=(const FreeRTOSRecursiveMutex&) = delete;

    private:
        SemaphoreHandle_t m_handle = nullptr;
    };

} // namespace dmq::os

#endif // FREERTOS_MUTEX_H