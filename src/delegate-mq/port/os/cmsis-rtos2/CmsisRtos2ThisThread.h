#ifndef CMSIS_RTOS2_THIS_THREAD_H
#define CMSIS_RTOS2_THIS_THREAD_H

/// @file CmsisRtos2ThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, CMSIS-RTOS2 backend.
///
/// @details
/// This is dmq::ThisThread for this port (see DelegateOpt.h). GetCurrent()
/// uses RegistryCurrentThread, keyed by the calling thread's id. Exists so
/// library internals that need to delay or yield (e.g. RetryMonitor backoff)
/// aren't forced to pull in the full dmq::os::Thread class -- which also
/// drags in the message queue, watchdog, and stats machinery -- just to
/// sleep. dmq::os::Thread::Sleep() forwards here too, so the CMSIS-RTOS2
/// delay call is implemented exactly once.

#include "cmsis_os2.h"
#include "CmsisRtos2Mutex.h"
#include "port/os/common/CurrentThreadStorage.h"
#include <chrono>

namespace dmq::os {

    /// @brief Key for RegistryCurrentThread: the calling thread's id.
    /// @TODO: Return 0 inside an ISR if your target can detect it (e.g.
    /// __get_IPSR() != 0 on ARM Cortex-M). CMSIS-RTOS2 has no portable ISR check,
    /// so dmq::ThisThread::GetCurrent() from an ISR returns the interrupted
    /// thread's IThread instead of nullptr. See docs/PORTING.md, "Current Thread".
    inline uintptr_t CmsisRtos2CurrentThreadKey() {
        return reinterpret_cast<uintptr_t>(osThreadGetId());
    }

    struct CmsisRtos2ThisThread : RegistryCurrentThread<CmsisRtos2CurrentThreadKey, CmsisRtos2Mutex> {
        template<typename Rep, typename Period>
        static void sleep_for(std::chrono::duration<Rep, Period> d) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);
            osDelay(static_cast<uint32_t>(ms.count()));
        }

        static void yield() noexcept {
            osThreadYield();
        }
    };

} // namespace dmq::os

#endif // CMSIS_RTOS2_THIS_THREAD_H
