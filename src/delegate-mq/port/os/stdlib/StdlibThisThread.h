#ifndef STDLIB_THIS_THREAD_H
#define STDLIB_THIS_THREAD_H

/// @file StdlibThisThread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief dmq::ThisThread for the desktop ports (stdlib, Win32, Qt, POSIX).
///
/// @details
/// std::this_thread is already portable on these platforms, so sleep_for() and
/// yield() forward to it. GetCurrent() uses thread_local storage.

#include "port/os/common/CurrentThreadStorage.h"
#include <chrono>
#include <thread>

namespace dmq::os {

    struct StdlibThisThread : ThreadLocalCurrentThread {
        template<typename Rep, typename Period>
        static void sleep_for(std::chrono::duration<Rep, Period> d) { std::this_thread::sleep_for(d); }

        /// @brief Sleep until dmq::Clock (std::chrono::steady_clock) reaches t;
        /// returns at once if it already has. For drift-free periodic loops:
        /// next += period; sleep_until(next).
        template<typename Duration>
        static void sleep_until(const std::chrono::time_point<std::chrono::steady_clock, Duration>& t) {
            std::this_thread::sleep_until(t);
        }

        static void yield() noexcept { std::this_thread::yield(); }
    };

} // namespace dmq::os

#endif // STDLIB_THIS_THREAD_H
