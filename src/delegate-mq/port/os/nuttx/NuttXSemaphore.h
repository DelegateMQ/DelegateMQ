#ifndef NUTTX_SEMAPHORE_H
#define NUTTX_SEMAPHORE_H

/// @file NuttXSemaphore.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief dmq::Semaphore backing for NuttX, using its native POSIX sem_t
/// directly instead of the generic dmq::ConditionVariable + dmq::Mutex
/// implementation in delegate/Semaphore.h.
///
/// @details
/// NuttX has no dmq::ConditionVariable port (no DMQ_HAS_CV) -- not because
/// it lacks pthread_cond_t (it has a real one), but because a native
/// sem_t is a more direct fit for what dmq::Semaphore actually needs, the
/// same reasoning ZephyrSemaphore.h and CmsisRtos2Semaphore.h give for
/// their own RTOSes: no need to build a semaphore out of a mutex +
/// condition variable when the platform already has a real one. DelegateOpt.h
/// resolves dmq::Semaphore to this type on NuttX; see the "Semaphore"
/// comment next to DMQ_THREAD_NUTTX's `using Semaphore = ...;` there.
///
/// @note This header is included from DelegateOpt.h's early port-include
/// block, before "namespace dmq { ... }" opens and dmq::Duration is defined
/// inside it (including it any later would nest as dmq::dmq::os instead of
/// dmq::os) -- so, like ZephyrSemaphore.h and CmsisRtos2Semaphore.h, Wait()
/// is templated on the chrono duration type instead of depending on
/// dmq::Duration directly. A dmq::Duration argument still deduces through
/// it seamlessly at the call site, since dmq::Duration is itself a
/// std::chrono::duration specialization.
///
/// @note sem_timedwait() takes an ABSOLUTE CLOCK_REALTIME deadline, not a
/// relative duration -- this computes that deadline from clock_gettime()
/// plus the caller's relative timeout, the standard POSIX idiom.
///
/// @note UNVERIFIED: written against documented NuttX POSIX API behavior,
/// same rigor as NuttXCriticalSection.h. No NuttX toolchain/simulator is
/// available in this development environment to build and run it.

#include <semaphore.h>
#include <time.h>
#include <chrono>

namespace dmq::os {

    // =========================================================================
    // NuttXSemaphore
    // Binary semaphore (max count 1), matching the semantics of the generic
    // dmq::Semaphore this replaces: Signal() when already signaled is a
    // no-op (not a counting semaphore), and Wait() consumes exactly one
    // pending signal.
    // =========================================================================
    class NuttXSemaphore {
    public:
        NuttXSemaphore() {
            sem_init(&m_sem, 0, 0);
        }

        ~NuttXSemaphore() {
            sem_destroy(&m_sem);
        }

        /// Called to wait on a semaphore to be signaled.
        /// @param[in] timeout - semaphore timeout
        /// @return Return true if semaphore signaled, false if timeout occurred.
        template<typename Rep, typename Period>
        bool Wait(const std::chrono::duration<Rep, Period>& timeout) {
            using DurationT = std::chrono::duration<Rep, Period>;
            if (timeout == DurationT::max()) {
                return sem_wait(&m_sem) == 0;
            }

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count();
            ts.tv_sec += static_cast<time_t>(ns / 1000000000LL);
            ts.tv_nsec += static_cast<long>(ns % 1000000000LL);
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_nsec -= 1000000000L;
                ts.tv_sec += 1;
            }
            return sem_timedwait(&m_sem, &ts) == 0;
        }

        /// Called to signal a semaphore.
        void Signal() {
            int val = 0;
            if (sem_getvalue(&m_sem, &val) == 0) {
                if (val <= 0) {
                    sem_post(&m_sem);
                }
            } else {
                // Fallback if sem_getvalue fails
                sem_post(&m_sem);
            }
        }

        NuttXSemaphore(const NuttXSemaphore&) = delete;
        NuttXSemaphore& operator=(const NuttXSemaphore&) = delete;

    private:
        sem_t m_sem;
    };

} // namespace dmq::os

#endif // NUTTX_SEMAPHORE_H
