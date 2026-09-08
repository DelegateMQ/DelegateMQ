#ifndef BARE_METAL_THIS_THREAD_H
#define BARE_METAL_THIS_THREAD_H

/// @file BareMetalThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Portable sleep_for()/yield() for the calling thread, bare-metal backend.
///
/// @details
/// Backs dmq::ThisThread::sleep_for()/yield() (see DelegateOpt.h). There is
/// no scheduler on DMQ_THREAD_NONE, so sleep_for() busy-waits against
/// BareMetalClock -- the same tick source Timer already keys off of -- rather
/// than pretending a real blocking delay exists. yield() is a no-op: with a
/// single thread of control (a superloop), there is nothing else to hand the
/// CPU to; hardware ISRs preempt regardless of this call.

#include "BareMetalClock.h"
#include <chrono>

namespace dmq::os {

    struct BareMetalThisThread {
        static void sleep_for(std::chrono::milliseconds ms) {
            auto start = BareMetalClock::now();
            while (BareMetalClock::now() - start < ms) {
                // Busy-wait: no scheduler to yield to on bare metal.
            }
        }

        static void yield() noexcept {
            // No-op: single thread of control on bare metal.
        }
    };

} // namespace dmq::os

#endif // BARE_METAL_THIS_THREAD_H
