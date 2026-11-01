#ifndef BARE_METAL_CLOCK_H
#define BARE_METAL_CLOCK_H

#include <chrono>
#include <cstdint>

// 1. Declare the external tick counter.
// This variable must be defined in your main.cpp or startup.c
// and incremented by your SysTick_Handler (or other timer ISR).
extern "C" volatile uint64_t g_ticks;

namespace dmq::os {

    struct BareMetalClock {
        // 2. Define duration traits
        // We assume g_ticks represents Milliseconds (1/1000 sec).
        // If your timer runs at a different rate (e.g. Microseconds), change std::milli to std::micro.
        using rep = int64_t;
        using period = std::milli;
        using duration = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<BareMetalClock>;

        static const bool is_steady = true;

        // 3. The critical "now()" function
        static time_point now() noexcept {
            // On a 32-bit target, reading a 64-bit value requires two 32-bit
            // loads. A timer ISR firing between them would produce a torn
            // read, so interrupts are masked for the read. The previous
            // interrupt state is saved and restored, not unconditionally
            // re-enabled: Timer calls this while holding its
            // BareMetalCriticalSection (Timer::Start(), and ProcessTimers() ->
            // CheckExpired(), possibly from the timer ISR itself), and turning
            // interrupts back on there would let the ISR run ProcessTimers()
            // against a half-updated timer list. Same technique as
            // BareMetalCriticalSection.
#if defined(__riscv)
            unsigned long prev;
            __asm__ volatile("csrrci %0, mstatus, 0x8" : "=r"(prev) :: "memory"); // clear MIE, save old
            uint64_t t = g_ticks;
            __asm__ volatile("csrs mstatus, %0" :: "r"(prev & 0x8ul) : "memory"); // restore MIE only if it was set
#elif defined(__arm__) && (defined(__GNUC__) || defined(__clang__))
            uint32_t prev;
            __asm__ volatile("mrs %0, primask" : "=r"(prev) :: "memory");
            __asm__ volatile("cpsid i" ::: "memory");
            uint64_t t = g_ticks;
            __asm__ volatile("msr primask, %0" :: "r"(prev) : "memory");
#elif defined(__CC_ARM) || defined(__ARMCC_VERSION)
            uint32_t prev = __get_PRIMASK();
            __disable_irq();
            uint64_t t = g_ticks;
            __set_PRIMASK(prev);
#else
            // Fallback: plain read (safe on platforms with atomic 64-bit loads)
            uint64_t t = g_ticks;
#endif
            return time_point(duration(static_cast<rep>(t)));
        }
    };
} // namespace dmq::os

#endif // BARE_METAL_CLOCK_H