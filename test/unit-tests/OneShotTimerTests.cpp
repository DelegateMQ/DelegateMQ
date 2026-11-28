// dmq::util::OneShotTimer (extras/util/OneShotTimer.h). The test runner's own
// thread calls Timer::ProcessTimers() every 5 ms, so timers fire by themselves.

#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std::chrono_literals;

namespace {

Thread& TestThread() { static Thread t("OneShotTimerTests"); return t; }

std::atomic<int> s_fired{0};
std::atomic<bool> s_onTestThread{false};
void OnFired()
{
    s_onTestThread = TestThread().IsCurrentThread();
    s_fired++;
}

bool WaitFired(int n, std::chrono::milliseconds timeout = 2000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (s_fired.load() < n && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(5ms);
    return s_fired.load() >= n;
}

// Holds the test thread busy, so a timer's expiry waits in its queue
std::atomic<bool> s_hold{false};
std::atomic<bool> s_holding{false};
void Hold()
{
    s_holding = true;
    while (s_hold.load())
        std::this_thread::sleep_for(1ms);
    s_holding = false;
}
void BlockThread()
{
    s_hold = true;
    dmq::MakeDelegate(&Hold, TestThread())();
    while (!s_holding.load())
        std::this_thread::sleep_for(1ms);
}
void ReleaseThread()
{
    s_hold = false;
    while (s_holding.load() || TestThread().GetQueueSize() != 0)
        std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(20ms);
}

std::atomic<int> s_other{0};
void OnOther() { s_other++; }

// A callback that replaces itself with another from inside the callback
struct Replacer {
    OneShotTimer timer{ &TestThread() };
    void Run() { timer.Start(10ms, dmq::MakeDelegate(&OnOther)); }
};

// A callback that destroys its own timer
struct SelfDestruct {
    std::unique_ptr<OneShotTimer> timer = std::make_unique<OneShotTimer>(&TestThread());
    std::atomic<bool> ran{false};
    void Run() { timer.reset(); ran = true; }
};

// A callback that restarts its own timer a few times
struct Repeater {
    OneShotTimer timer{ &TestThread() };
    std::atomic<int> runs{0};
    void Run()
    {
        if (++runs < 3)
            timer.Start(10ms);      // from inside the callback, same callback
    }
};

} // namespace

void OneShotTimerTests()
{
    if (!TestThread().CreateThread())
        DMQ_ASSERT_TRUE(false);

    // 1. Fires once, on the thread given, then is no longer running
    {
        s_fired = 0;
        OneShotTimer t(&TestThread());
        DMQ_ASSERT_TRUE(!t.IsRunning());
        t.Start(30ms, dmq::MakeDelegate(&OnFired));
        DMQ_ASSERT_TRUE(t.IsRunning());
        DMQ_ASSERT_TRUE(WaitFired(1));
        DMQ_ASSERT_TRUE(s_onTestThread.load());
        std::this_thread::sleep_for(100ms);
        DMQ_ASSERT_TRUE(s_fired.load() == 1);       // once, not periodic
        DMQ_ASSERT_TRUE(!t.IsRunning());
    }

    // 2. Start() again before it fires restarts the countdown (debounce)
    {
        s_fired = 0;
        OneShotTimer t(&TestThread());
        t.Start(300ms, dmq::MakeDelegate(&OnFired));
        std::this_thread::sleep_for(100ms);
        t.Start(300ms);                             // same callback, countdown restarts
        std::this_thread::sleep_for(100ms);         // 200 ms in: 200 ms of margin each way
        DMQ_ASSERT_TRUE(s_fired.load() == 0);
        DMQ_ASSERT_TRUE(WaitFired(1));
        std::this_thread::sleep_for(100ms);
        DMQ_ASSERT_TRUE(s_fired.load() == 1);
    }

    // 3. Stop() before it fires: never runs
    {
        s_fired = 0;
        OneShotTimer t(&TestThread());
        t.Start(50ms, dmq::MakeDelegate(&OnFired));
        t.Stop();
        DMQ_ASSERT_TRUE(!t.IsRunning());
        std::this_thread::sleep_for(150ms);
        DMQ_ASSERT_TRUE(s_fired.load() == 0);
    }

    // 4. Stop() after the expiry is already queued to the busy thread: it
    //    still doesn't run (a plain Timer's queued callback would)
    {
        s_fired = 0;
        OneShotTimer t(&TestThread());
        BlockThread();
        t.Start(10ms, dmq::MakeDelegate(&OnFired));
        std::this_thread::sleep_for(80ms);          // expired: its call waits in the queue
        DMQ_ASSERT_TRUE(TestThread().GetQueueSize() >= 1);
        t.Stop();
        ReleaseThread();
        DMQ_ASSERT_TRUE(s_fired.load() == 0);
    }

    // 5. Restart while the old expiry is queued: the old one is ignored, the
    //    new one runs once, later
    {
        s_fired = 0;
        OneShotTimer t(&TestThread());
        BlockThread();
        t.Start(10ms, dmq::MakeDelegate(&OnFired));
        std::this_thread::sleep_for(80ms);
        t.Start(200ms);
        ReleaseThread();
        DMQ_ASSERT_TRUE(s_fired.load() == 0);       // the stale expiry did nothing
        DMQ_ASSERT_TRUE(WaitFired(1));
        std::this_thread::sleep_for(100ms);
        DMQ_ASSERT_TRUE(s_fired.load() == 1);
    }

    // 6. Destroyed while its expiry is queued: the callback doesn't run, and
    //    nothing touches the destroyed timer
    {
        s_fired = 0;
        auto t = std::make_unique<OneShotTimer>(&TestThread());
        BlockThread();
        t->Start(10ms, dmq::MakeDelegate(&OnFired));
        std::this_thread::sleep_for(80ms);
        t.reset();
        ReleaseThread();
        DMQ_ASSERT_TRUE(s_fired.load() == 0);
    }

    // 7. The callback may restart its own timer
    {
        Repeater r;
        r.timer.Start(10ms, dmq::MakeDelegate(&r, &Repeater::Run));
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (r.runs.load() < 3 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(5ms);
        std::this_thread::sleep_for(100ms);
        DMQ_ASSERT_TRUE(r.runs.load() == 3);
        DMQ_ASSERT_TRUE(!r.timer.IsRunning());
    }

    // 8. A shorter restart while an earlier expiry is still queued: the queued
    //    one belongs to an older Start() and is ignored even though the new
    //    deadline has passed by the time it's handled
    {
        s_fired = 0;
        OneShotTimer t(&TestThread());
        BlockThread();
        t.Start(10ms, dmq::MakeDelegate(&OnFired));
        std::this_thread::sleep_for(80ms);          // expiry 1 queued
        t.Stop();
        t.Start(5ms);
        std::this_thread::sleep_for(80ms);          // expiry 2 queued, deadline passed
        ReleaseThread();
        DMQ_ASSERT_TRUE(s_fired.load() == 1);       // once: expiry 2 only
        DMQ_ASSERT_TRUE(!t.IsRunning());
    }

    // 9. Start(timeout, other) from inside the callback replaces it
    {
        s_other = 0;
        Replacer r;
        s_fired = 0;
        r.timer.Start(10ms, dmq::MakeDelegate(&r, &Replacer::Run));
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (s_other.load() < 1 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(5ms);
        std::this_thread::sleep_for(100ms);
        DMQ_ASSERT_TRUE(s_other.load() == 1);
        DMQ_ASSERT_TRUE(!r.timer.IsRunning());
    }

    // 10. SetThread() after construction, as an owner that gets its thread later
    {
        s_fired = 0;
        OneShotTimer t;
        t.SetThread(&TestThread());
        t.Start(20ms, dmq::MakeDelegate(&OnFired));
        DMQ_ASSERT_TRUE(WaitFired(1));
        DMQ_ASSERT_TRUE(s_onTestThread.load());
    }

    // 11. The callback destroys its own timer
    {
        SelfDestruct d;
        d.timer->Start(10ms, dmq::MakeDelegate(&d, &SelfDestruct::Run));
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!d.ran.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(5ms);
        DMQ_ASSERT_TRUE(d.ran.load());
        DMQ_ASSERT_TRUE(d.timer == nullptr);
    }

    // 12. No thread: runs on the thread that calls Timer::ProcessTimers()
    {
        s_fired = 0;
        OneShotTimer t;
        t.Start(20ms, dmq::MakeDelegate(&OnFired));
        DMQ_ASSERT_TRUE(WaitFired(1));
        DMQ_ASSERT_TRUE(!s_onTestThread.load());
    }

    TestThread().ExitThread();
    std::cout << "OneShotTimerTests PASSED!" << std::endl;
}
