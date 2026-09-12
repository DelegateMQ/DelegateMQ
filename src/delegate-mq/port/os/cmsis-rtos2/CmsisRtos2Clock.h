#ifndef CMSIS_RTOS2_CLOCK_H
#define CMSIS_RTOS2_CLOCK_H

#include "cmsis_os2.h"
#include "CmsisRtos2CriticalSection.h"
#include <chrono>

namespace dmq::os {
    struct CmsisRtos2Clock {
        // Assume 1 tick = 1 millisecond.
        // If your RTOS tick is different, change std::milli to your ratio.
        using rep = int64_t;
        using period = std::milli;
        using duration = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<CmsisRtos2Clock>;
        static const bool is_steady = true;

        static time_point now() noexcept {
            // Static state to track the 32-bit rollover.
            // osKernelGetTickCount() wraps every ~49.7 days (at 1ms tick).

            // NOTE: This implementation relies on static state.
            // It must be called at least once every 49 days to detect the wrap.
            //
            // Thread/ISR safety: dmq::util::Timer::ProcessTimers() (the only
            // caller that needs 'last'/'high' updated atomically) is documented
            // as callable from ISR context, and on CMSIS-RTOS2/Zephyr that's a
            // real requirement, not a hypothetical -- a k_timer expiry_fn (which
            // is what a CMSIS-RTOS2 osTimer callback compiles down to under
            // Zephyr's compatibility layer) runs in genuine ISR context per
            // Zephyr's own kernel.h. osKernelLock()/osKernelRestoreLock() are
            // NOT ISR-safe -- Zephyr's implementation returns osErrorISR from
            // osKernelLock() when called from an ISR, but osKernelRestoreLock()
            // unconditionally writes that error code into the interrupted
            // thread's own scheduler-lock-nesting count before it even checks
            // for ISR context, corrupting scheduler state (verified: this is
            // exactly what caused a one-shot dmq::util::Timer to refire forever
            // instead of once, in example/sample-projects/cmsis-rtos2-linux).
            // dmq::CriticalSection is the primitive this library already uses
            // everywhere else for "must work from both thread and ISR context"
            // (see CLAUDE.md's "ISR-Safe Locking" section) -- used here for the
            // same reason.
            static uint32_t last = 0;
            static uint64_t high = 0;

            CmsisRtos2CriticalSection lock;
            lock.lock();

            uint32_t cur = osKernelGetTickCount();

            // Check for wrap-around (current time is less than last seen time)
            if (cur < last) {
                // Add 2^32 to the high part accumulator
                high += 0x100000000ULL;
            }

            last = cur;

            // Combine the high part with the current low part.
            uint64_t ticks = high + cur;

            lock.unlock();

            return time_point(duration(static_cast<rep>(ticks)));
        }
    };
} // namespace dmq::os

#endif // CMSIS_RTOS2_CLOCK_H