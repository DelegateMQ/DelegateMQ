#ifndef FREERTOS_CRITICAL_SECTION_H
#define FREERTOS_CRITICAL_SECTION_H

/// @file FreeRTOSCriticalSection.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief ISR-safe critical section for FreeRTOS.
///
/// @details
/// Unlike ThreadX/Zephyr/CMSIS-RTOS2, FreeRTOS has no single primitive that
/// is safe to call from BOTH thread and ISR context: it splits critical
/// sections into two separate APIs -- taskENTER_CRITICAL()/EXIT() for task
/// context, taskENTER_CRITICAL_FROM_ISR()/EXIT_FROM_ISR() for ISR context --
/// and expects the caller to already know which one applies. Calling the
/// wrong one for the current context is undefined behavior.
///
/// This type resolves that at runtime via xPortIsInsideInterrupt(), so
/// lock()/unlock() work correctly from either context without the caller
/// needing to know which one it's in -- exactly the contract
/// dmq::util::Timer::GetLock() needs, since Timer::ProcessTimers() is
/// documented as callable from the highest-priority context available.
///
/// xPortIsInsideInterrupt() is provided by every real ARM Cortex-M FreeRTOS
/// port this project supports (ARM_CM3, CM4F, CM7, CM23, CM33, CM55 --
/// e.g. the boards/stm32-freertos sample), detected via reading the ARM Cortex-M
/// core register whose value is meaningless outside a real Cortex-M CPU.
/// The desktop simulator ports (POSIX, Win32 -- used by os/freertos,
/// os/freertos-bare-metal, databus/freertos) do not provide it: they run as
/// an ordinary host-OS thread with no real hardware interrupt to detect,
/// so "always task context" is the correct (and only meaningful) answer
/// there. dmq::os::detail::IsInsideFreeRTOSInterrupt() below selects
/// between the two at compile time via the __arm__/__ARM_ARCH compiler
/// predefines (set by arm-none-eabi-gcc, never set when GCC/Clang builds
/// the desktop simulators for x86_64) -- no new DMQ_* build option needed,
/// and no risk of colliding with FreeRTOS's own xPortIsInsideInterrupt()
/// symbol, since we never redefine it under that name ourselves.
///
/// ESP-IDF (ESP32 family, Xtensa and RISC-V, including Arduino-ESP32) is
/// SMP FreeRTOS: a critical section is a spinlock (portMUX_TYPE) shared by
/// both cores, and portENTER_CRITICAL_SAFE() picks the task or ISR form
/// itself. ISR detection there is xPortInIsrContext(). Selected by
/// ESP_PLATFORM, ahead of the ARM check.
///
/// @note Scoping assumption: other than ESP-IDF, this assumes ARM Cortex-M
/// is the embedded target. Another RISC-V or Xtensa FreeRTOS port would
/// need the detection below extended for its own architecture predefines.
///
/// *** NARROW PURPOSE -- DO NOT USE THIS AS A GENERAL-PURPOSE LOCK ***
/// On the ISR path this masks interrupts up to configMAX_SYSCALL_INTERRUPT_PRIORITY
/// for as long as it is held; on the task path it disables the scheduler
/// and masks interrupts the same way. Only use it to protect something
/// genuinely tiny and bounded that may be touched from ISR context. Do NOT
/// use it in place of dmq::Mutex / dmq::RecursiveMutex for ordinary
/// thread-to-thread synchronization (e.g. DataBus internals, a Thread's
/// message queue) -- anything that can block, take a while, or run for an
/// unbounded time must not run with interrupts globally masked; that is a
/// real-time correctness bug on hardware, not just a style issue.
///
/// *** NOT RE-LOCKABLE ON THE SAME INSTANCE ***
/// Unlike dmq::RecursiveMutex, calling lock() twice on the SAME instance
/// without an intervening unlock() is incorrect: the second lock() call
/// overwrites the single saved-interrupt-state member with the (already
/// masked) inner state, so the outer unlock() restores the wrong state.
/// There is no "recursive" variant of this type -- it has no ownership
/// concept to make recursion meaningful, and Timer's own usage never
/// nests. Locking two DIFFERENT instances in proper LIFO lock/unlock
/// order is fine: each instance saves and restores its own state
/// independently.

#include "FreeRTOSHeaders.h"

namespace dmq::os {

    namespace detail {
#if defined(ESP_PLATFORM)
        // ESP-IDF names its ISR-context check differently.
        inline BaseType_t IsInsideFreeRTOSInterrupt() { return xPortInIsrContext() ? pdTRUE : pdFALSE; }
#elif defined(__arm__) || defined(__ARM_ARCH)
        // Real ARM Cortex-M FreeRTOS port -- it provides
        // xPortIsInsideInterrupt() itself; call the real thing.
        inline BaseType_t IsInsideFreeRTOSInterrupt() { return xPortIsInsideInterrupt(); }
#else
        // Desktop simulator port (POSIX/Win32) -- no real hardware
        // interrupt to detect; always task context.
        inline BaseType_t IsInsideFreeRTOSInterrupt() { return pdFALSE; }
#endif
    } // namespace detail

    // =========================================================================
    // FreeRTOSCriticalSection
    // See the file-level comment above before using this type anywhere new.
    // =========================================================================
#if defined(ESP_PLATFORM)
    class FreeRTOSCriticalSection {
    public:
        FreeRTOSCriticalSection() = default;

        // _SAFE selects the task or ISR form from the current context.
        void lock() { portENTER_CRITICAL_SAFE(&m_mux); }
        void unlock() { portEXIT_CRITICAL_SAFE(&m_mux); }

        FreeRTOSCriticalSection(const FreeRTOSCriticalSection&) = delete;
        FreeRTOSCriticalSection& operator=(const FreeRTOSCriticalSection&) = delete;

    private:
        // SMP: a spinlock shared by both cores, not just interrupt masking.
        portMUX_TYPE m_mux = portMUX_INITIALIZER_UNLOCKED;
    };
#else
    class FreeRTOSCriticalSection {
    public:
        FreeRTOSCriticalSection() = default;

        void lock() {
            if (detail::IsInsideFreeRTOSInterrupt() != pdFALSE) {
                m_savedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
                m_lockedFromIsr = true;
            } else {
                taskENTER_CRITICAL();
                m_lockedFromIsr = false;
            }
        }

        void unlock() {
            if (m_lockedFromIsr) {
                taskEXIT_CRITICAL_FROM_ISR(m_savedInterruptStatus);
            } else {
                taskEXIT_CRITICAL();
            }
        }

        // No try_lock(): interrupt masking cannot fail to "acquire", so a
        // try_lock() here would always trivially succeed and isn't meaningful.

        FreeRTOSCriticalSection(const FreeRTOSCriticalSection&) = delete;
        FreeRTOSCriticalSection& operator=(const FreeRTOSCriticalSection&) = delete;

    private:
        UBaseType_t m_savedInterruptStatus = 0;
        bool m_lockedFromIsr = false;
    };
#endif // ESP_PLATFORM

} // namespace dmq::os

#endif // FREERTOS_CRITICAL_SECTION_H
