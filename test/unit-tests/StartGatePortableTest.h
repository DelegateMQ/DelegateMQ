#ifndef START_GATE_PORTABLE_TEST_H
#define START_GATE_PORTABLE_TEST_H

/// @file
/// @brief dmq::os::StartGate tests that use only DelegateMQ's portable APIs
/// (no std::thread), so the same file runs on every thread port: the desktop
/// unit tests include it, and so does each RTOS sample's DelegateThreadsTests.cpp
/// (FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2, NuttX, POSIX). A failed check
/// faults through DMQ_ASSERT_TRUE.

#include "DelegateMQ.h"
#include <atomic>
#include <chrono>
#include <cstdio>

#if defined(DMQ_HAS_START_GATE)

namespace StartGatePortableTest {

inline std::atomic<int>& Ran() { static std::atomic<int> ran{0}; return ran; }
inline void Count(int) { Ran()++; }
inline void SleepMs(int ms) { dmq::ThisThread::sleep_for(std::chrono::milliseconds(ms)); }

/// Poll for up to 3 s; the held threads run asynchronously after Open().
inline bool WaitForRan(int expected)
{
    for (int i = 0; i < 300 && Ran().load() != expected; i++)
        SleepMs(10);
    return Ran().load() == expected;
}

// Created and ready, but nothing runs at any priority until Open(). Two
// threads at one gate, or one where the port can't run two at once (see Run()).
inline void HoldsUntilOpen(bool twoThreads)
{
    dmq::os::StartGate gate;
    dmq::os::Thread a("GateA");
    dmq::os::Thread b("GateB");
    a.SetStartGate(&gate);
    b.SetStartGate(&gate);
    DMQ_ASSERT_TRUE(a.CreateThread());
    if (twoThreads)
        DMQ_ASSERT_TRUE(b.CreateThread());
    Ran() = 0;
    for (dmq::os::Thread* t : { &a, &b })
    {
        if (t == &b && !twoThreads)
            break;
        dmq::MakeDelegate(&Count, *t)(0);
        auto high = dmq::MakeDelegate(&Count, *t);
        high.SetPriority(dmq::Priority::HIGH);   // must wait at the gate too
        high(0);
    }
    SleepMs(100);
    DMQ_ASSERT_TRUE(Ran() == 0);
    gate.Open();
    DMQ_ASSERT_TRUE(WaitForRan(twoThreads ? 4 : 2));
    a.ExitThread();
    b.ExitThread();
}

// ExitThread() while held, with a full queue: returns, runs nothing. On RTOS
// ports the exit message send blocks while the queue is full, so this also
// checks the held thread drains it. Re-created, it holds again until Open().
inline void ExitWhileHeld()
{
    dmq::os::StartGate gate;
    dmq::os::Thread t("GateExit", 4, dmq::FullPolicy::DROP);
    t.SetStartGate(&gate);
    DMQ_ASSERT_TRUE(t.CreateThread());
    Ran() = 0;
    for (int i = 0; i < 6; i++)
        dmq::MakeDelegate(&Count, t)(0);   // 4 queued, 2 dropped
    t.ExitThread();
    DMQ_ASSERT_TRUE(Ran() == 0);

    DMQ_ASSERT_TRUE(t.CreateThread());
    dmq::MakeDelegate(&Count, t)(0);
    SleepMs(50);
    DMQ_ASSERT_TRUE(Ran() == 0);
    gate.Open();
    DMQ_ASSERT_TRUE(WaitForRan(1));
    t.ExitThread();
}

// Close() right after Open(), before the released thread can run: it must
// still start (it was released), not be held again.
inline void OpenThenCloseStillReleases()
{
    for (int i = 0; i < 20; i++)
    {
        dmq::os::StartGate gate;
        dmq::os::Thread t("GateOpenClose");
        t.SetStartGate(&gate);
        DMQ_ASSERT_TRUE(t.CreateThread());
        Ran() = 0;
        dmq::MakeDelegate(&Count, t)(0);
        gate.Open();
        gate.Close();
        DMQ_ASSERT_TRUE(WaitForRan(1));
        t.ExitThread();
    }
}

// Held longer than its watchdog timeout: not reported (a report ends the
// program in WatchdogHandler()). Released, it keeps its own watchdog fed.
inline void WatchdogIgnoresHeldThread()
{
    dmq::os::StartGate gate;
    dmq::os::Thread t("GateWatchdog");
    t.SetStartGate(&gate);
    DMQ_ASSERT_TRUE(t.CreateThread(std::chrono::milliseconds(100)));
    for (int i = 0; i < 20; i++)
    {
        dmq::os::Thread::WatchdogCheckAll();
        SleepMs(20);
    }
    gate.Open();
    for (int i = 0; i < 15; i++)
    {
        dmq::os::Thread::WatchdogCheckAll();
        SleepMs(20);
    }
    t.ExitThread();
}

/// @param twoThreads false where the port can't have two DelegateMQ threads
/// alive at once (the ThreadX Linux simulator; see threadx-linux's
/// DelegateThreadsTests.cpp). Every other test uses one thread at a time.
inline void Run(bool twoThreads = true)
{
    HoldsUntilOpen(twoThreads);
    ExitWhileHeld();
    OpenThenCloseStillReleases();
    WatchdogIgnoresHeldThread();
    printf("StartGatePortableTest::Run() complete!\n");
}

} // namespace StartGatePortableTest

#endif // DMQ_HAS_START_GATE

#endif // START_GATE_PORTABLE_TEST_H
