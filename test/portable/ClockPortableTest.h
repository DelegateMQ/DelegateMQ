#ifndef CLOCK_PORTABLE_TEST_H
#define CLOCK_PORTABLE_TEST_H

/// @file
/// @brief dmq::HighResClock and dmq::ThisThread::sleep_until() on any thread
/// port, with portable APIs only. Run by DelegateThreadsPortableTests.cpp on
/// every OS port sample, by the desktop unit tests, and by the FreeRTOS
/// Cortex-M suite (os/freertos-bare-metal). A failed check faults through
/// DMQ_ASSERT_TRUE. Call from a thread the port knows (an RTOS task).
///
/// - HighResClock never goes backward, agrees with dmq::Clock over a sleep,
///   and (highRes) resolves a span shorter than 1 ms.
/// - sleep_until() returns at once for a time already past, never wakes early,
///   and a periodic loop with a body that takes time doesn't drift: 10 periods
///   take 10 periods, not 10 x (period + body).

#include "DelegateMQ.h"
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace ClockPortableTest {

using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::milliseconds;

inline long long Ms(dmq::Duration d) {
    return static_cast<long long>(duration_cast<milliseconds>(d).count());
}

/// @param highRes the port's HighResClock has a sub-millisecond source and
/// time advances while a thread spins: false on bare metal and on the
/// simulators (Zephyr native_sim, NuttX sim), whose time stands still then
inline void Run(bool highRes = true)
{
    // 1. HighResClock: monotonic, and the smallest step it shows. Bounded by a
    //    count too: on the simulators (Zephyr native_sim, NuttX sim) time
    //    doesn't advance while a thread spins
    {
        auto prev = dmq::HighResClock::now();
        auto smallest = dmq::HighResClock::duration::max();
        const auto deadline = dmq::Clock::now() + milliseconds(20);
        for (uint32_t i = 0; i < 2000000u && dmq::Clock::now() < deadline; i++) {
            const auto now = dmq::HighResClock::now();
            DMQ_ASSERT_TRUE(now >= prev);
            if (now > prev && now - prev < smallest)
                smallest = now - prev;
            prev = now;
        }
        const bool stepped = smallest != dmq::HighResClock::duration::max();
        const long long stepNs = stepped ?
            static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(smallest).count()) : -1;
        printf("ClockPortableTest: HighResClock smallest step %ld ns\n", static_cast<long>(stepNs));
        if (highRes)
            DMQ_ASSERT_TRUE(stepped && smallest < milliseconds(1));
    }

    // 2. HighResClock agrees with dmq::Clock over a 50 ms sleep
    {
        const auto hrStart = dmq::HighResClock::now();
        const auto start = dmq::Clock::now();
        dmq::ThisThread::sleep_for(milliseconds(50));
        const long long hrUs = static_cast<long long>(
            duration_cast<microseconds>(dmq::HighResClock::now() - hrStart).count());
        const long long ms = Ms(dmq::Clock::now() - start);
        printf("ClockPortableTest: 50 ms sleep: HighResClock %ld us, Clock %ld ms\n",
               static_cast<long>(hrUs), static_cast<long>(ms));
        DMQ_ASSERT_TRUE(hrUs >= 45000 && hrUs < 500000);
        DMQ_ASSERT_TRUE(hrUs / 1000 >= ms - 12 && hrUs / 1000 <= ms + 12);
    }

    // 3. sleep_until() a time already past returns at once, also one before
    //    the clock's start (a negative count, e.g. now - 5 ms just after boot)
    {
        const auto start = dmq::Clock::now();
        dmq::ThisThread::sleep_until(start - milliseconds(5));
        dmq::ThisThread::sleep_until(start);
        dmq::ThisThread::sleep_until(dmq::TimePoint{} - milliseconds(5));
        dmq::ThisThread::sleep_until(dmq::TimePoint{});
        // Generous: a desktop CI runner may preempt the thread for a while
        DMQ_ASSERT_TRUE(Ms(dmq::Clock::now() - start) <= 15);
    }

    // 4. A periodic loop doesn't drift: each period's body (10 ms) is absorbed
    {
        constexpr int PERIODS = 10;
        constexpr auto PERIOD = milliseconds(20);
        const auto start = dmq::Clock::now();
        auto next = start;
        for (int i = 0; i < PERIODS; i++) {
            dmq::ThisThread::sleep_for(milliseconds(10));   // the loop's work
            next += PERIOD;
            dmq::ThisThread::sleep_until(next);
            DMQ_ASSERT_TRUE(dmq::Clock::now() >= next);     // never early
        }
        const long long total = Ms(dmq::Clock::now() - start);
        printf("ClockPortableTest: %d x 20 ms periods (10 ms work each) took %ld ms\n", PERIODS, static_cast<long>(total));
        // sleep_for(PERIOD) would take 300 ms; allow a late wake at the end
        // (a 15.6 ms Windows timer tick plus jitter)
        DMQ_ASSERT_TRUE(total >= 200 && total < 260);
    }

    printf("ClockPortableTest PASSED\n");
}

} // namespace ClockPortableTest

#endif // CLOCK_PORTABLE_TEST_H
