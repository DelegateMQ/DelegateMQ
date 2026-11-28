#ifndef PRIORITY_PORTABLE_TEST_H
#define PRIORITY_PORTABLE_TEST_H

/// @file
/// @brief dmq::os::ThreadPriority bands and Thread::SetPriority() on any thread
/// port, with portable APIs only. Run by DelegateThreadsPortableTests.cpp on
/// every OS port sample, by the desktop unit tests, and by the FreeRTOS
/// Cortex-M suite (os/freertos-bare-metal). A failed check faults through
/// DMQ_ASSERT_TRUE.
///
/// - The band arithmetic every RTOS port uses (port/os/common/ThreadPriority.h):
///   the spread across a range either way round, steps within a band and the
///   refusal of a step onto the next band, and bands set by the config.
/// - SetPriority() on every band, before and after CreateThread(): the thread
///   still runs. On an RTOS port it always succeeds; on the desktop the OS may
///   refuse (Linux, raising a thread unprivileged), which is only reported.
/// - priorityOrder: two threads made ready together run the more urgent band
///   first. Single-core schedulers only: on more than one core both may run
///   at once.

#include "DelegateMQ.h"
#include "port/os/common/ThreadPriority.h"
#include <atomic>
#include <chrono>
#include <cstdio>

namespace PriorityPortableTest {

using dmq::os::ThreadPriority;
namespace pd = dmq::os::detail;

inline bool Resolves(const pd::PriorityBands& b, ThreadPriority band, int step, int expected)
{
    int native = -1000;
    return pd::ResolvePriority(b, band, step, native) && native == expected;
}

inline bool Refuses(const pd::PriorityBands& b, ThreadPriority band, int step)
{
    int native = 0;
    return !pd::ResolvePriority(b, band, step, native);
}

/// The arithmetic, with made-up ranges: runs the same on every port
inline void BandMath()
{
    // Higher is more urgent (FreeRTOS, 32 levels)
    const auto up = pd::SpreadPriorityBands(0, 15, 31);
    const int upBands[7] = { 0, 5, 10, 15, 20, 25, 31 };
    for (int r = 0; r < 7; r++)
        DMQ_ASSERT_TRUE(up.band[r] == upBands[r]);
    DMQ_ASSERT_TRUE(Resolves(up, ThreadPriority::NORMAL, 0, 15));
    DMQ_ASSERT_TRUE(Resolves(up, ThreadPriority::NORMAL, 4, 19));
    DMQ_ASSERT_TRUE(Refuses(up, ThreadPriority::NORMAL, 5));        // ABOVE_NORMAL's level
    DMQ_ASSERT_TRUE(Resolves(up, ThreadPriority::NORMAL, -4, 11));
    DMQ_ASSERT_TRUE(Refuses(up, ThreadPriority::NORMAL, -5));       // BELOW_NORMAL's level
    DMQ_ASSERT_TRUE(Refuses(up, ThreadPriority::TIME_CRITICAL, 1)); // past the top
    DMQ_ASSERT_TRUE(Resolves(up, ThreadPriority::TIME_CRITICAL, -5, 26));
    DMQ_ASSERT_TRUE(Refuses(up, ThreadPriority::IDLE, -1));         // past the bottom
    DMQ_ASSERT_TRUE(Resolves(up, ThreadPriority::IDLE, 4, 4));

    // Lower is more urgent (ThreadX, 32 levels): a +1 step is one number lower
    const auto down = pd::SpreadPriorityBands(31, 15, 0);
    const int downBands[7] = { 31, 26, 21, 15, 10, 5, 0 };
    for (int r = 0; r < 7; r++)
        DMQ_ASSERT_TRUE(down.band[r] == downBands[r]);
    DMQ_ASSERT_TRUE(Resolves(down, ThreadPriority::NORMAL, 4, 11));
    DMQ_ASSERT_TRUE(Refuses(down, ThreadPriority::NORMAL, 5));
    DMQ_ASSERT_TRUE(Resolves(down, ThreadPriority::NORMAL, -5, 20));
    DMQ_ASSERT_TRUE(Refuses(down, ThreadPriority::NORMAL, -6));
    DMQ_ASSERT_TRUE(Refuses(down, ThreadPriority::TIME_CRITICAL, 1));

    // Zephyr: the bands span the preemptible priorities; a step above
    // TIME_CRITICAL may reach the cooperative ones
    auto zephyr = pd::SpreadPriorityBands(14, 7, 0);
    zephyr.most = -16;
    DMQ_ASSERT_TRUE(Resolves(zephyr, ThreadPriority::TIME_CRITICAL, 1, -1));
    DMQ_ASSERT_TRUE(Refuses(zephyr, ThreadPriority::TIME_CRITICAL, 17));

    // Few levels (FreeRTOS, 5): bands share levels, and a step has no room
    const auto few = pd::SpreadPriorityBands(0, 2, 4);
    const int fewBands[7] = { 0, 0, 1, 2, 2, 3, 4 };
    for (int r = 0; r < 7; r++)
        DMQ_ASSERT_TRUE(few.band[r] == fewBands[r]);
    DMQ_ASSERT_TRUE(Resolves(few, ThreadPriority::ABOVE_NORMAL, 0, 2));
    DMQ_ASSERT_TRUE(Refuses(few, ThreadPriority::NORMAL, 1));

    // CMSIS-RTOS2's named levels (osPriorityLow 8 ... osPriorityRealtime7 55)
    const pd::PriorityBands cmsis = { { 8, 12, 16, 24, 32, 40, 48 }, 8, 55 };
    DMQ_ASSERT_TRUE(Resolves(cmsis, ThreadPriority::NORMAL, 7, 31));    // osPriorityNormal7
    DMQ_ASSERT_TRUE(Refuses(cmsis, ThreadPriority::NORMAL, 8));         // osPriorityAboveNormal
    DMQ_ASSERT_TRUE(Resolves(cmsis, ThreadPriority::TIME_CRITICAL, 7, 55));

    // The config places bands (DMQ_PRIORITY_BAND_*): HIGHEST kept below a
    // vendor task at 23; the others stay where the port puts them
    const int A = dmq::PRIORITY_BAND_AUTO;
    const int keepBelow23[7] = { A, A, A, A, A, 22, A };
    const auto placed = pd::ApplyPriorityBandConfig(up, keepBelow23);
    DMQ_ASSERT_TRUE(placed.band[5] == 22 && placed.band[4] == 20 && placed.band[6] == 31);
    DMQ_ASSERT_TRUE(Resolves(placed, ThreadPriority::HIGHEST, 0, 22));
    DMQ_ASSERT_TRUE(Resolves(placed, ThreadPriority::ABOVE_NORMAL, 1, 21));
    DMQ_ASSERT_TRUE(Refuses(placed, ThreadPriority::ABOVE_NORMAL, 2));

    // A config that puts the bands out of order, or outside the port's range,
    // is refused for every band
    const int outOfOrder[7] = { A, A, A, 24, A, A, A };   // NORMAL above ABOVE_NORMAL (20)
    const int outOfRange[7] = { A, A, A, A, A, A, 40 };
    for (int r = 0; r < 7; r++) {
        DMQ_ASSERT_TRUE(Refuses(pd::ApplyPriorityBandConfig(up, outOfOrder), static_cast<ThreadPriority>(r), 0));
        DMQ_ASSERT_TRUE(Refuses(pd::ApplyPriorityBandConfig(up, outOfRange), static_cast<ThreadPriority>(r), 0));
    }
    // The config on a port where lower is more urgent: HIGHEST at 8
    const int highestAt8[7] = { A, A, A, A, A, 8, A };
    const auto placedDown = pd::ApplyPriorityBandConfig(down, highestAt8);
    DMQ_ASSERT_TRUE(Resolves(placedDown, ThreadPriority::HIGHEST, 0, 8));
    DMQ_ASSERT_TRUE(Resolves(placedDown, ThreadPriority::ABOVE_NORMAL, 1, 9));
    DMQ_ASSERT_TRUE(Refuses(placedDown, ThreadPriority::ABOVE_NORMAL, 2));
    const int normalAbove[7] = { A, A, A, 9, A, A, A };          // more urgent than ABOVE_NORMAL (10)
    DMQ_ASSERT_TRUE(Refuses(pd::ApplyPriorityBandConfig(down, normalAbove), ThreadPriority::NORMAL, 0));

    // One level: every band on it, no step
    const auto one = pd::SpreadPriorityBands(0, 0, 0);
    for (int r = 0; r < 7; r++)
        DMQ_ASSERT_TRUE(Resolves(one, static_cast<ThreadPriority>(r), 0, 0));
    DMQ_ASSERT_TRUE(Refuses(one, ThreadPriority::NORMAL, 1));
    DMQ_ASSERT_TRUE(Refuses(one, ThreadPriority::NORMAL, -1));

    // Not a band, and steps at the ends of int
    DMQ_ASSERT_TRUE(Refuses(up, static_cast<ThreadPriority>(7), 0));
    for (const auto* b : { &up, &down }) {
        DMQ_ASSERT_TRUE(Refuses(*b, ThreadPriority::NORMAL, 2147483647));
        DMQ_ASSERT_TRUE(Refuses(*b, ThreadPriority::NORMAL, -2147483647 - 1));
        DMQ_ASSERT_TRUE(Refuses(*b, ThreadPriority::TIME_CRITICAL, 2147483647));
        DMQ_ASSERT_TRUE(Refuses(*b, ThreadPriority::IDLE, -2147483647 - 1));
    }
    printf("PriorityPortableTest: band arithmetic OK\n");
}

inline std::atomic<int>& Ran() { static std::atomic<int> ran{ 0 }; return ran; }
inline void Count() { Ran()++; }

/// Wait up to 2 s for Ran() to reach n
inline bool WaitForRan(int n)
{
    for (int i = 0; i < 400 && Ran().load() < n; i++)
        dmq::ThisThread::sleep_for(std::chrono::milliseconds(5));
    return Ran().load() >= n;
}

/// true on the RTOS ports: SetPriority() always applies there
inline constexpr bool ALWAYS_APPLIES =
#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_POSIX) || defined(DMQ_THREAD_QT)
    false;
#else
    true;
#endif

/// SetPriority() on every band, before and after CreateThread()
inline void EveryBand()
{
    // NORMAL on a running thread at its default priority: every port, also
    // desktops that refuse other bands (macOS) or raising (Linux unprivileged)
    {
        Ran() = 0;
        dmq::os::Thread t("PrioNormal");
        DMQ_ASSERT_TRUE(t.CreateThread());
        DMQ_ASSERT_TRUE(t.SetPriority(ThreadPriority::NORMAL));
#if defined(__APPLE__)
        DMQ_ASSERT_TRUE(!t.SetPriority(ThreadPriority::HIGHEST));   // no per-thread priority here
#endif
        // Not a band: a desktop port refuses it (an RTOS port faults)
        if (!ALWAYS_APPLIES)
            DMQ_ASSERT_TRUE(!t.SetPriority(static_cast<ThreadPriority>(7)));
        dmq::MakeDelegate(&Count, t)();
        DMQ_ASSERT_TRUE(WaitForRan(1));
        t.ExitThread();
        dmq::ThisThread::sleep_for(std::chrono::milliseconds(5));
    }

    int refused = 0;
    for (int r = 0; r < 7; r++) {
        const auto band = static_cast<ThreadPriority>(r);
        Ran() = 0;
        dmq::os::Thread t("PrioBand");
        DMQ_ASSERT_TRUE(t.SetPriority(band));      // stored until the thread starts
        DMQ_ASSERT_TRUE(t.CreateThread());
        dmq::MakeDelegate(&Count, t)();
        DMQ_ASSERT_TRUE(WaitForRan(1));

        // Changed while it runs, to every band and back
        for (int to = 0; to < 7; to++) {
            const bool applied = t.SetPriority(static_cast<ThreadPriority>(to));
            if (ALWAYS_APPLIES)
                DMQ_ASSERT_TRUE(applied);
            else if (!applied)
                refused++;
        }
        (void)t.SetPriority(band);
        dmq::MakeDelegate(&Count, t)();
        DMQ_ASSERT_TRUE(WaitForRan(2));
        t.ExitThread();
        dmq::ThisThread::sleep_for(std::chrono::milliseconds(5));   // let the kernel free it
    }
    printf("PriorityPortableTest: every band set before and after CreateThread() (%d refused by the OS)\n", refused);
}

/// Order(): who ran, in order. A thread claims a slot (OrderClaim()), writes
/// it, then counts itself done (OrderDone(), release), so a reader that sees
/// OrderDone() == 2 (acquire) sees both letters, even if it preempted a thread
/// between its claim and its write.
inline std::atomic<int>& OrderClaim() { static std::atomic<int> n{ 0 }; return n; }
inline std::atomic<int>& OrderDone() { static std::atomic<int> n{ 0 }; return n; }
inline char* Order() { static char order[3] = { 0, 0, 0 }; return order; }
inline void Mark(char c)
{
    const int i = OrderClaim()++;
    if (i < 2)
        Order()[i] = c;
    OrderDone().fetch_add(1, std::memory_order_release);
}

/// The two threads PostBoth() posts to (an async call copies what a pointer
/// argument points at, so they aren't passed as arguments)
inline dmq::os::Thread*& LessUrgent() { static dmq::os::Thread* t = nullptr; return t; }
inline dmq::os::Thread*& MoreUrgent() { static dmq::os::Thread* t = nullptr; return t; }

/// Post to the less urgent thread first, then the more urgent one. Runs on a
/// thread more urgent than both, so neither runs until this returns.
inline void PostBoth()
{
    dmq::MakeDelegate(&Mark, *LessUrgent())('L');
    dmq::MakeDelegate(&Mark, *MoreUrgent())('M');
}

/// Two threads made ready together: the more urgent band runs first
inline void UrgentRunsFirst()
{
    OrderClaim() = 0;
    OrderDone() = 0;
    Order()[0] = Order()[1] = 0;
    dmq::os::Thread less("PrioLess");
    dmq::os::Thread more("PrioMore");
    dmq::os::Thread poster("PrioPoster");
    less.SetPriority(ThreadPriority::BELOW_NORMAL);
    more.SetPriority(ThreadPriority::ABOVE_NORMAL);
    poster.SetPriority(ThreadPriority::HIGHEST);
    DMQ_ASSERT_TRUE(less.CreateThread());
    DMQ_ASSERT_TRUE(more.CreateThread());
    DMQ_ASSERT_TRUE(poster.CreateThread());

    LessUrgent() = &less;
    MoreUrgent() = &more;
    dmq::MakeDelegate(&PostBoth, poster)();
    for (int i = 0; i < 400 && OrderDone().load(std::memory_order_acquire) < 2; i++)
        dmq::ThisThread::sleep_for(std::chrono::milliseconds(5));
    const bool bothRan = OrderDone().load(std::memory_order_acquire) == 2;
    printf("PriorityPortableTest: ran in order %c%c (M = ABOVE_NORMAL, L = BELOW_NORMAL)\n",
           Order()[0] ? Order()[0] : '-', Order()[1] ? Order()[1] : '-');
    DMQ_ASSERT_TRUE(bothRan);
    DMQ_ASSERT_TRUE(Order()[0] == 'M' && Order()[1] == 'L');

    poster.ExitThread();
    more.ExitThread();
    less.ExitThread();
    dmq::ThisThread::sleep_for(std::chrono::milliseconds(5));
}

/// @param priorityOrder the port has one core and a priority scheduler: check
/// that the more urgent band runs first
inline void Run(bool priorityOrder)
{
    BandMath();
    EveryBand();
    if (priorityOrder)
        UrgentRunsFirst();
    printf("PriorityPortableTest PASSED\n");
}

} // namespace PriorityPortableTest

#endif // PRIORITY_PORTABLE_TEST_H
