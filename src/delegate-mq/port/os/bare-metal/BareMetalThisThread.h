#ifndef BARE_METAL_THIS_THREAD_H
#define BARE_METAL_THIS_THREAD_H

/// @file BareMetalThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, bare-metal backend.
///
/// @details
/// This is dmq::ThisThread for this port (see DelegateOpt.h). GetCurrent()
/// has no per-thread storage here yet and returns nullptr. There is
/// no scheduler on DMQ_THREAD_NONE, so sleep_for() busy-waits against
/// BareMetalClock -- the same tick source Timer already keys off of -- rather
/// than pretending a real blocking delay exists. yield() is a no-op: with a
/// single thread of control (a superloop), there is nothing else to hand the
/// CPU to; hardware ISRs preempt regardless of this call.

#include "BareMetalClock.h"
#include "port/os/common/CurrentThreadStorage.h"
#include <chrono>

namespace dmq::os {

    struct BareMetalThisThread : NoCurrentThread {
        template<typename Rep, typename Period>
        static void sleep_for(std::chrono::duration<Rep, Period> d) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);
            auto start = BareMetalClock::now();
            while (BareMetalClock::now() - start < ms) {
                // Busy-wait: no scheduler to yield to on bare metal.
            }
        }

        /// @brief Busy-wait until dmq::Clock (g_ticks) reaches t; returns at
        /// once if it already has. For drift-free periodic loops: next +=
        /// period; sleep_until(next).
        template<typename Duration>
        static void sleep_until(const std::chrono::time_point<BareMetalClock, Duration>& t) {
            while (BareMetalClock::now() < t) {
            }
        }

        static void yield() noexcept {
            // No-op: single thread of control on bare metal.
        }
    };

} // namespace dmq::os

#endif // BARE_METAL_THIS_THREAD_H
