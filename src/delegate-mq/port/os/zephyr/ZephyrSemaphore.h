#ifndef ZEPHYR_SEMAPHORE_H
#define ZEPHYR_SEMAPHORE_H

/// @file ZephyrSemaphore.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief dmq::Semaphore backing for Zephyr, using Zephyr's native k_sem
/// directly instead of the generic dmq::ConditionVariable + dmq::Mutex
/// implementation in delegate/Semaphore.h.
///
/// @details
/// Zephyr has no dmq::ConditionVariable port (no DMQ_HAS_CV), so the
/// generic Semaphore implementation isn't available here -- but Zephyr
/// already provides a native binary semaphore, which is a more direct fit
/// anyway (no need to build one out of a mutex + condition variable when
/// the RTOS already has a real one). DelegateOpt.h resolves dmq::Semaphore
/// to this type on Zephyr; see the "Semaphore" comment next to
/// DMQ_THREAD_ZEPHYR's `using Semaphore = ...;` there.
///
/// @note This header is included from DelegateOpt.h's early port-include
/// block, before "namespace dmq { ... }" opens and dmq::Duration is defined
/// inside it (including it any later would nest as dmq::dmq::os instead of
/// dmq::os) -- so, like ThreadXConditionVariable.h and
/// FreeRTOSConditionVariable.h, Wait() is templated on the chrono duration
/// type instead of depending on dmq::Duration directly. A dmq::Duration
/// argument still deduces through it seamlessly at the call site, since
/// dmq::Duration is itself a std::chrono::duration specialization.
///
/// @note UNVERIFIED: written against documented Zephyr kernel API behavior,
/// same rigor as ZephyrCriticalSection.h. No Zephyr SDK/west workspace is
/// available in this development environment to build and run it.

#include <zephyr/kernel.h>
#include <chrono>

namespace dmq::os {

    // =========================================================================
    // ZephyrSemaphore
    // Binary semaphore (max count 1), matching the semantics of the generic
    // dmq::Semaphore this replaces: Signal() when already signaled is a
    // no-op (not a counting semaphore), and Wait() consumes exactly one
    // pending signal.
    // =========================================================================
    class ZephyrSemaphore {
    public:
        ZephyrSemaphore() {
            k_sem_init(&m_sem, 0, 1);
        }

        ~ZephyrSemaphore() = default;

        /// Called to wait on a semaphore to be signaled.
        /// @param[in] timeout - semaphore timeout
        /// @return Return true if semaphore signaled, false if timeout occurred.
        template<typename Rep, typename Period>
        bool Wait(const std::chrono::duration<Rep, Period>& timeout) {
            using DurationT = std::chrono::duration<Rep, Period>;
            if (timeout == DurationT::max()) {
                return k_sem_take(&m_sem, K_FOREVER) == 0;
            }
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
            return k_sem_take(&m_sem, K_MSEC(ms)) == 0;
        }

        /// Called to signal a semaphore.
        void Signal() {
            k_sem_give(&m_sem);
        }

        ZephyrSemaphore(const ZephyrSemaphore&) = delete;
        ZephyrSemaphore& operator=(const ZephyrSemaphore&) = delete;

    private:
        struct k_sem m_sem;
    };

} // namespace dmq::os

#endif // ZEPHYR_SEMAPHORE_H
