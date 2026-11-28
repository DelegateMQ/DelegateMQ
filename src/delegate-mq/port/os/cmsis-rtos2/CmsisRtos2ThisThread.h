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
#include "CmsisRtos2CriticalSection.h"
#include "CmsisRtos2Ticks.h"
#include "CmsisRtos2Clock.h"
#include "port/os/common/CurrentThreadStorage.h"
#include <chrono>

namespace dmq::os {

    /// @brief Key for RegistryCurrentThread: the calling thread's id, or 0 inside
    /// an ISR (detected on Arm Cortex-M through IPSR; see
    /// detail::IsInsideCmsisRtos2Interrupt() in CmsisRtos2CriticalSection.h).
    /// @TODO: On a non-Cortex-M CMSIS-RTOS2 target (e.g. Cortex-A), extend
    /// detail::IsInsideCmsisRtos2Interrupt() for it; until then,
    /// dmq::ThisThread::GetCurrent() from an ISR returns the interrupted thread's
    /// IThread instead of nullptr. See docs/PORTING.md, "Current Thread".
    inline uintptr_t CmsisRtos2CurrentThreadKey() {
        if (detail::IsInsideCmsisRtos2Interrupt())
            return 0;
        return reinterpret_cast<uintptr_t>(osThreadGetId());
    }

    struct CmsisRtos2ThisThread : RegistryCurrentThread<CmsisRtos2CurrentThreadKey, CmsisRtos2Mutex> {
        template<typename Rep, typename Period>
        static void sleep_for(std::chrono::duration<Rep, Period> d) {
            osDelay(CmsisRtos2ToTicks(d));  // kernel ticks, rounded up
        }

        /// @brief Sleep until dmq::Clock reaches t; returns at once if it
        /// already has. For drift-free periodic loops: next += period;
        /// sleep_until(next). osDelayUntil() takes an absolute tick, so a
        /// preemption before the call doesn't delay the wake.
        template<typename Duration>
        static void sleep_until(const std::chrono::time_point<CmsisRtos2Clock, Duration>& t) {
            // Target in kernel ticks, rounded up so the wake is never early
            const int64_t raw = static_cast<int64_t>(
                std::chrono::ceil<std::chrono::milliseconds>(t.time_since_epoch()).count());
            if (raw <= 0)
                return;     // before the clock's start: already past
            const uint64_t ms = static_cast<uint64_t>(raw);
            const uint64_t freq = CmsisRtos2TickFreq();
            const uint64_t target = (ms / 1000u) * freq + ((ms % 1000u) * freq + 999u) / 1000u;
            // osDelayUntil() rejects a target more than half the 32-bit tick
            // range ahead: a longer wait goes in steps
            constexpr uint64_t MAX_STEP = 0x7FFFFFFFu;
            for (;;) {
                const uint64_t now = CmsisRtos2Clock::NowTicks();
                if (now >= target)
                    return;
                const uint64_t step = (target - now > MAX_STEP) ? now + MAX_STEP : target;
                // One tick left: a relative delay. An absolute one could see
                // the tick pass before the call, and not every layer rejects
                // a past target (Zephyr's sleeps for the wrapped difference)
                // A target that passes before the call returns at once
                // (osErrorParameter on RTX and FreeRTOS); the loop then sees
                // it reached. Any other error (called from an ISR, kernel not
                // running) can't sleep at all: return rather than spin
                const osStatus_t status = (target - now <= 1u)
                    ? osDelay(1u)
                    : osDelayUntil(static_cast<uint32_t>(step));
                if (status != osOK && status != osErrorParameter)
                    return;
            }
        }

        static void yield() noexcept {
            osThreadYield();
        }
    };

} // namespace dmq::os

#endif // CMSIS_RTOS2_THIS_THREAD_H
