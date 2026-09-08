#ifndef CMSIS_RTOS2_SEMAPHORE_H
#define CMSIS_RTOS2_SEMAPHORE_H

/// @file CmsisRtos2Semaphore.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief dmq::Semaphore backing for CMSIS-RTOS2, using osSemaphore
/// directly instead of the generic dmq::ConditionVariable + dmq::Mutex
/// implementation in delegate/Semaphore.h.
///
/// @details
/// CMSIS-RTOS2 has no dmq::ConditionVariable port (no DMQ_HAS_CV) -- and no
/// native condition-variable primitive to build one from, unlike Zephyr's
/// k_condvar. It does have a native binary semaphore (osSemaphore), which
/// is a direct fit for what dmq::Semaphore actually needs: no need to build
/// one out of a mutex + condition variable when the RTOS already has a real
/// one. DelegateOpt.h resolves dmq::Semaphore to this type on CMSIS-RTOS2;
/// see the "Semaphore" comment next to DMQ_THREAD_CMSIS_RTOS2's
/// `using Semaphore = ...;` there.
///
/// @note This header is included from DelegateOpt.h's early port-include
/// block, before "namespace dmq { ... }" opens and dmq::Duration is defined
/// inside it (including it any later would nest as dmq::dmq::os instead of
/// dmq::os) -- so, like the other CMSIS-RTOS2 port files, Wait() is
/// templated on the chrono duration type instead of depending on
/// dmq::Duration directly. A dmq::Duration argument still deduces through
/// it seamlessly at the call site, since dmq::Duration is itself a
/// std::chrono::duration specialization.
///
/// @note UNVERIFIED: written against documented CMSIS-RTOS2 API behavior,
/// same rigor as CmsisRtos2CriticalSection.h. No CMSIS-RTOS2 SDK (e.g. Keil
/// RTX5) or Cortex-M hardware/QEMU target is available in this development
/// environment to build and run it.
///
/// @note osSemaphoreAcquire()'s timeout parameter is in RTOS kernel ticks,
/// not literally milliseconds -- like CmsisRtos2Clock.h, this assumes a
/// 1ms tick (the common CMSIS-RTOS2 default). If your target's tick rate
/// differs, convert accordingly before calling Wait().

#include "cmsis_os2.h"
#include <cassert>
#include <chrono>

namespace dmq::os {

    // =========================================================================
    // CmsisRtos2Semaphore
    // Binary semaphore (max count 1), matching the semantics of the generic
    // dmq::Semaphore this replaces: Signal() when already signaled is a
    // no-op (not a counting semaphore), and Wait() consumes exactly one
    // pending signal.
    // =========================================================================
    class CmsisRtos2Semaphore {
    public:
        CmsisRtos2Semaphore() {
            m_id = osSemaphoreNew(1, 0, nullptr);
            assert(m_id != nullptr);
        }

        ~CmsisRtos2Semaphore() {
            if (m_id) osSemaphoreDelete(m_id);
        }

        /// Called to wait on a semaphore to be signaled.
        /// @param[in] timeout - semaphore timeout
        /// @return Return true if semaphore signaled, false if timeout occurred.
        template<typename Rep, typename Period>
        bool Wait(const std::chrono::duration<Rep, Period>& timeout) {
            if (!m_id) return false;
            using DurationT = std::chrono::duration<Rep, Period>;
            uint32_t ticks;
            if (timeout == DurationT::max()) {
                ticks = osWaitForever;
            } else {
                ticks = static_cast<uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
            }
            return osSemaphoreAcquire(m_id, ticks) == osOK;
        }

        /// Called to signal a semaphore.
        void Signal() {
            if (m_id) osSemaphoreRelease(m_id);
        }

        CmsisRtos2Semaphore(const CmsisRtos2Semaphore&) = delete;
        CmsisRtos2Semaphore& operator=(const CmsisRtos2Semaphore&) = delete;

    private:
        osSemaphoreId_t m_id = nullptr;
    };

} // namespace dmq::os

#endif // CMSIS_RTOS2_SEMAPHORE_H
