/// @file TimerDelegatePortableTests.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
///
/// @brief PacedDispatch, TimerDelegate and Timer tests on any thread port,
/// from test/unit-tests/TimerDelegateTests.cpp. One copy for every OS port
/// sample (see PortableTests.h for what the sample must provide, and
/// DelegateThreadsPortableTests.cpp for why only portable APIs are used).
///
/// Unlike the unit tests, nothing here starts a thread to drive
/// Timer::ProcessTimers(): the sample already does (an RTOS timer task, or a
/// helper thread on the desktop), and a raw host thread isn't safe on an RTOS
/// simulation.

#include "PortableTests.h"
#include "extras/util/TimerDelegate.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <memory>
#include <functional>
#include <vector>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace {

Thread& timerThread()
{
    static Thread t("TimerDelegateTests");
    return t;
}

void SleepMs(int ms) { dmq::ThisThread::sleep_for(std::chrono::milliseconds(ms)); }

// Wait until count reaches expected or timeout elapses. Returns true on success.
bool WaitCount(const std::atomic<int>& cnt, int expected,
               std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
{
    auto deadline = dmq::Clock::now() + timeout;
    while (cnt.load() < expected && dmq::Clock::now() < deadline)
        SleepMs(10);
    return cnt.load() >= expected;
}

// Wait for thread queue to drain and give current message time to finish.
void Drain(Thread& t)
{
    while (t.GetQueueSize() != 0)
        SleepMs(10);
    SleepMs(100);
}

// =============================================================================
// PacedDispatch unit tests (no threads required)
// =============================================================================

void PacedDispatch_TryFireClearSucceeds()
{
    PacedDispatch gate;
    bool fired = false;
    bool result = gate.TryFire([&fired](std::shared_ptr<DispatchToken>) { fired = true; });
    DMQ_ASSERT_TRUE(result == true);
    DMQ_ASSERT_TRUE(fired == true);
    DMQ_ASSERT_TRUE(!gate.IsInFlight()); // token not captured -> expired immediately
    std::cout << "PacedDispatch_TryFireClearSucceeds() complete!" << std::endl;
}

void PacedDispatch_TryFireInFlightFails()
{
    PacedDispatch gate;
    std::shared_ptr<DispatchToken> liveToken;

    bool first = gate.TryFire([&liveToken](std::shared_ptr<DispatchToken> t) { liveToken = t; });
    DMQ_ASSERT_TRUE(first == true);
    DMQ_ASSERT_TRUE(gate.IsInFlight());

    bool second = gate.TryFire([](std::shared_ptr<DispatchToken>) {});
    DMQ_ASSERT_TRUE(second == false);
    std::cout << "PacedDispatch_TryFireInFlightFails() complete!" << std::endl;
}

void PacedDispatch_TryFireAfterTokenReleased()
{
    PacedDispatch gate;
    std::shared_ptr<DispatchToken> liveToken;

    gate.TryFire([&liveToken](std::shared_ptr<DispatchToken> t) { liveToken = t; });
    DMQ_ASSERT_TRUE(gate.IsInFlight());

    liveToken.reset(); // simulate message completion
    DMQ_ASSERT_TRUE(!gate.IsInFlight());

    bool result = gate.TryFire([](std::shared_ptr<DispatchToken>) {});
    DMQ_ASSERT_TRUE(result == true);
    std::cout << "PacedDispatch_TryFireAfterTokenReleased() complete!" << std::endl;
}

void PacedDispatch_Reset()
{
    PacedDispatch gate;
    std::shared_ptr<DispatchToken> liveToken;

    gate.TryFire([&liveToken](std::shared_ptr<DispatchToken> t) { liveToken = t; });
    DMQ_ASSERT_TRUE(gate.IsInFlight());

    gate.Reset();
    DMQ_ASSERT_TRUE(!gate.IsInFlight());

    bool result = gate.TryFire([](std::shared_ptr<DispatchToken>) {});
    DMQ_ASSERT_TRUE(result == true);
    std::cout << "PacedDispatch_Reset() complete!" << std::endl;
}

void PacedDispatch_PendingFlag()
{
    PacedDispatch gate;
    std::shared_ptr<DispatchToken> liveToken;

    // First fire — succeeds, sets in-flight
    bool first = gate.TryFire([&liveToken](std::shared_ptr<DispatchToken> t) { liveToken = t; });
    DMQ_ASSERT_TRUE(first == true);
    DMQ_ASSERT_TRUE(gate.IsInFlight());
    DMQ_ASSERT_TRUE(!gate.IsPending());

    // Second fire — fails, sets pending
    bool second = gate.TryFire([](std::shared_ptr<DispatchToken>) {});
    DMQ_ASSERT_TRUE(second == false);
    DMQ_ASSERT_TRUE(gate.IsPending());

    // Release token
    liveToken.reset();
    DMQ_ASSERT_TRUE(!gate.IsInFlight());
    DMQ_ASSERT_TRUE(gate.IsPending());

    // Third fire — succeeds because was pending, clears pending
    bool third = gate.TryFire([](std::shared_ptr<DispatchToken>) {});
    DMQ_ASSERT_TRUE(third == true);
    DMQ_ASSERT_TRUE(!gate.IsPending());

    std::cout << "PacedDispatch_PendingFlag() complete!" << std::endl;
}

void PacedDispatch_OnStuckFires()
{
    PacedDispatch gate;
    std::shared_ptr<DispatchToken> liveToken;
    std::atomic<int> stuckCount{ 0 };
    gate.OnStuck = dmq::MakeDelegate(std::function<void()>([&stuckCount]() { stuckCount.fetch_add(1); }));

    gate.TryFire([&liveToken](std::shared_ptr<DispatchToken> t) { liveToken = t; });

    // Sleep past the stuck threshold, then trigger with a short stuckTimeout
    SleepMs(10);
    gate.TryFire([](std::shared_ptr<DispatchToken>) {}, std::chrono::milliseconds(1));

    DMQ_ASSERT_TRUE(stuckCount.load() >= 1);
    std::cout << "PacedDispatch_OnStuckFires() complete!" << std::endl;
}

// =============================================================================
// TimerDelegate async dispatch tests
// =============================================================================

struct TDTarget
{
    std::atomic<int> count{ 0 };
    void OnTick() { count.fetch_add(1); }
    void OnTickConst() const { /* const dispatch verification — just needs to compile and run */ }
    void OnTickSlow()
    {
        // Sleeps long enough that rapid timer ticks arrive while this is executing,
        // exercising the at-most-one-in-flight guarantee.
        SleepMs(150);
        count.fetch_add(1);
    }
};

void TimerDelegate_RawPtr_Dispatches()
{
    TDTarget target;
    auto d = MakeTimerDelegate(&target, &TDTarget::OnTick, timerThread());
    d();
    DMQ_ASSERT_TRUE(WaitCount(target.count, 1));
    Drain(timerThread());
    std::cout << "TimerDelegate_RawPtr_Dispatches() complete!" << std::endl;
}

void TimerDelegate_ConstRawPtr_Dispatches()
{
    std::atomic<int> count{ 0 };
    struct ConstTarget
    {
        std::atomic<int>* pCount;
        void OnTick() const { pCount->fetch_add(1); }
    };
    ConstTarget ct{ &count };
    const ConstTarget* cptr = &ct;

    auto d = MakeTimerDelegate(cptr, &ConstTarget::OnTick, timerThread());
    d();
    DMQ_ASSERT_TRUE(WaitCount(count, 1));
    Drain(timerThread());
    std::cout << "TimerDelegate_ConstRawPtr_Dispatches() complete!" << std::endl;
}

void TimerDelegate_AtMostOneInFlight()
{
    TDTarget target;
    auto d = MakeTimerDelegate(&target, &TDTarget::OnTickSlow, timerThread());

    // Fire 10 times in rapid succession. Only the first goes through;
    // the remaining 9 are skipped because the token is alive during the 150ms sleep.
    for (int i = 0; i < 10; i++)
        d();

    DMQ_ASSERT_TRUE(WaitCount(target.count, 1, std::chrono::milliseconds(3000)));
    Drain(timerThread());
    DMQ_ASSERT_TRUE(target.count.load() == 1);
    std::cout << "TimerDelegate_AtMostOneInFlight() complete!" << std::endl;
}

void TimerDelegate_FiresAgainAfterCompletion()
{
    TDTarget target;
    auto d = MakeTimerDelegate(&target, &TDTarget::OnTickSlow, timerThread());

    // First dispatch
    d();
    DMQ_ASSERT_TRUE(WaitCount(target.count, 1, std::chrono::milliseconds(3000)));
    Drain(timerThread()); // ensures token is expired before firing again

    // Second dispatch — token is now free
    d();
    DMQ_ASSERT_TRUE(WaitCount(target.count, 2, std::chrono::milliseconds(3000)));
    Drain(timerThread());

    DMQ_ASSERT_TRUE(target.count.load() == 2);
    std::cout << "TimerDelegate_FiresAgainAfterCompletion() complete!" << std::endl;
}

void TimerDelegate_SharedPtr_Dispatches()
{
    auto target = std::make_shared<TDTarget>();
    auto d = MakeTimerDelegate(target, &TDTarget::OnTick, timerThread());
    d();
    DMQ_ASSERT_TRUE(WaitCount(target->count, 1));
    Drain(timerThread());
    std::cout << "TimerDelegate_SharedPtr_Dispatches() complete!" << std::endl;
}

void TimerDelegate_SharedPtr_SkipsDestroyedObject()
{
    auto target = std::make_shared<TDTarget>();
    auto d = MakeTimerDelegate(target, &TDTarget::OnTick, timerThread());

    // First fire — object alive
    d();
    DMQ_ASSERT_TRUE(WaitCount(target->count, 1));
    Drain(timerThread());
    DMQ_ASSERT_TRUE(target->count.load() == 1);

    // Destroy object; delegate holds only a weak_ptr
    target.reset();

    // Second fire — weak_ptr.lock() returns null, dispatch silently skipped
    d();
    Drain(timerThread()); // no crash expected
    std::cout << "TimerDelegate_SharedPtr_SkipsDestroyedObject() complete!" << std::endl;
}

// =============================================================================
// TimerDelegate::Equal() / Empty() / nullptr comparisons
// =============================================================================

void FreeFuncForEqualityTests() {}

void TimerDelegate_Equal_SameUnderlyingAndThread()
{
    TDTarget target;
    auto d1 = MakeTimerDelegate(&target, &TDTarget::OnTick, timerThread());
    auto d2 = MakeTimerDelegate(&target, &TDTarget::OnTick, timerThread());
    DMQ_ASSERT_TRUE(d1.Equal(d2));
    DMQ_ASSERT_TRUE(d2.Equal(d1));
    std::cout << "TimerDelegate_Equal_SameUnderlyingAndThread() complete!" << std::endl;
}

void TimerDelegate_Equal_DifferentTargetObject()
{
    TDTarget target1;
    TDTarget target2;
    auto d1 = MakeTimerDelegate(&target1, &TDTarget::OnTick, timerThread());
    auto d2 = MakeTimerDelegate(&target2, &TDTarget::OnTick, timerThread());
    DMQ_ASSERT_TRUE(!d1.Equal(d2));
    std::cout << "TimerDelegate_Equal_DifferentTargetObject() complete!" << std::endl;
}

// Needs a second thread alive beside timerThread() (Options::concurrentThreads)
void TimerDelegate_Equal_DifferentThread()
{
    Thread otherThread("TimerDelegateTests_OtherThread");
    otherThread.CreateThread();

    TDTarget target;
    auto d1 = MakeTimerDelegate(&target, &TDTarget::OnTick, timerThread());
    auto d2 = MakeTimerDelegate(&target, &TDTarget::OnTick, otherThread);
    DMQ_ASSERT_TRUE(!d1.Equal(d2));

    otherThread.ExitThread();
    std::cout << "TimerDelegate_Equal_DifferentThread() complete!" << std::endl;
}

void TimerDelegate_Equal_DifferentDelegateType()
{
    // rhs isn't a TimerDelegate at all -- exercises dynamic_cast failing
    // safely and Equal() returning false rather than throwing/crashing.
    TDTarget target;
    auto d1 = MakeTimerDelegate(&target, &TDTarget::OnTick, timerThread());
    auto other = dmq::MakeDelegate(&FreeFuncForEqualityTests);
    DMQ_ASSERT_TRUE(!d1.Equal(other));
    std::cout << "TimerDelegate_Equal_DifferentDelegateType() complete!" << std::endl;
}

void TimerDelegate_Equal_BothEmpty()
{
    TDTarget target;
    auto d1 = MakeTimerDelegate(&target, &TDTarget::OnTick, timerThread());
    auto d2 = MakeTimerDelegate(&target, &TDTarget::OnTick, timerThread());
    d1.Clear();
    d2.Clear();
    DMQ_ASSERT_TRUE(d1.Empty());
    DMQ_ASSERT_TRUE(d2.Empty());
    DMQ_ASSERT_TRUE(d1.Equal(d2));
    std::cout << "TimerDelegate_Equal_BothEmpty() complete!" << std::endl;
}

void TimerDelegate_EmptyAndNullptrComparisons()
{
    TDTarget target;
    auto d = MakeTimerDelegate(&target, &TDTarget::OnTick, timerThread());
    DMQ_ASSERT_TRUE(!d.Empty());
    DMQ_ASSERT_TRUE(d != nullptr);
    DMQ_ASSERT_TRUE(!(d == nullptr));

    d.Clear();
    DMQ_ASSERT_TRUE(d.Empty());
    DMQ_ASSERT_TRUE(d == nullptr);
    DMQ_ASSERT_TRUE(!(d != nullptr));

    std::cout << "TimerDelegate_EmptyAndNullptrComparisons() complete!" << std::endl;
}

// =============================================================================
// Integration: TimerDelegate wired to a real Timer
// =============================================================================

// The sample's own timer drives Timer::ProcessTimers() (see PortableTests.h)
void TimerDelegate_WithTimer_DispatchesToThread()
{
    TDTarget target;
    Timer timer;
    dmq::ScopedConnection conn = timer.OnExpired.Connect(
        MakeTimerDelegate(&target, &TDTarget::OnTick, timerThread()));

    timer.Start(std::chrono::milliseconds(20));
    DMQ_ASSERT_TRUE(WaitCount(target.count, 3, std::chrono::milliseconds(2000)));

    timer.Stop();
    Drain(timerThread());

    DMQ_ASSERT_TRUE(target.count.load() >= 3);
    std::cout << "TimerDelegate_WithTimer_DispatchesToThread() complete!" << std::endl;
}

void Timer_ProcessTimers_DrainsMultiplePasses()
{
    // More than MAX_TIMER_EXPIRED timers all due at once — ProcessTimers() must
    // drain every batch, not just the first, and must not hard-fault (see
    // Timer::ProcessTimers()'s multi-pass loop, mirroring
    // TransportMonitor::Process()'s batch-cap handling).
    const size_t timerCount = dmq::MAX_TIMER_EXPIRED + 5;

    std::vector<std::unique_ptr<Timer>> timers;
    std::vector<dmq::ScopedConnection> conns;
    std::atomic<int> fireCount{ 0 };

    for (size_t i = 0; i < timerCount; ++i) {
        auto timer = std::make_unique<Timer>();
        conns.push_back(timer->OnExpired.Connect(
            dmq::MakeDelegate(std::function<void()>([&fireCount]() { fireCount.fetch_add(1); }))));
        timer->Start(std::chrono::milliseconds(1), /*once=*/true);
        timers.push_back(std::move(timer));
    }

    // Let every one-shot timer become due.
    SleepMs(20);

    // OnExpired has no thread argument, so callbacks fire synchronously inside
    // ProcessTimers(). The sample's own timer may be running ProcessTimers()
    // at the same time and fire some of them, so wait for all rather than
    // expect them in this one call (the unit tests, with no other caller,
    // check the single call).
    Timer::ProcessTimers();
    DMQ_ASSERT_TRUE(WaitCount(fireCount, static_cast<int>(timerCount), std::chrono::milliseconds(1000)));
    DMQ_ASSERT_TRUE(fireCount.load() == static_cast<int>(timerCount));
    std::cout << "Timer_ProcessTimers_DrainsMultiplePasses() complete!" << std::endl;
}

} // namespace

namespace PortableTests {

void TimerDelegateTests(const Options& options)
{
    timerThread().CreateThread();

    PacedDispatch_TryFireClearSucceeds();
    PacedDispatch_TryFireInFlightFails();
    PacedDispatch_TryFireAfterTokenReleased();
    PacedDispatch_Reset();
    PacedDispatch_PendingFlag();
    PacedDispatch_OnStuckFires();

    TimerDelegate_RawPtr_Dispatches();
    TimerDelegate_ConstRawPtr_Dispatches();
    TimerDelegate_AtMostOneInFlight();
    TimerDelegate_FiresAgainAfterCompletion();
    TimerDelegate_SharedPtr_Dispatches();
    TimerDelegate_SharedPtr_SkipsDestroyedObject();

    TimerDelegate_Equal_SameUnderlyingAndThread();
    TimerDelegate_Equal_DifferentTargetObject();
    if (options.concurrentThreads)
        TimerDelegate_Equal_DifferentThread();
    TimerDelegate_Equal_DifferentDelegateType();
    TimerDelegate_Equal_BothEmpty();
    TimerDelegate_EmptyAndNullptrComparisons();

    TimerDelegate_WithTimer_DispatchesToThread();
    Timer_ProcessTimers_DrainsMultiplePasses();

    timerThread().ExitThread();
    std::cout << "TimerDelegateTests() complete!" << std::endl;
}

} // namespace PortableTests
