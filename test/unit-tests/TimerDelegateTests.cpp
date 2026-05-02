#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include "extras/util/TimerDelegate.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

static Thread s_thread("TimerDelegateTests");

// Wait until count reaches expected or timeout elapses. Returns true on success.
static bool WaitCount(const std::atomic<int>& cnt, int expected,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (cnt.load() != expected && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return cnt.load() == expected;
}

// Wait for thread queue to drain and give current message time to finish.
static void Drain(Thread& t)
{
    while (t.GetQueueSize() != 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// =============================================================================
// PacedDispatch unit tests (no threads required)
// =============================================================================

static void PacedDispatch_TryFireClearSucceeds()
{
    PacedDispatch gate;
    bool fired = false;
    bool result = gate.TryFire([&fired](std::shared_ptr<DispatchToken>) { fired = true; });
    ASSERT_TRUE(result == true);
    ASSERT_TRUE(fired == true);
    ASSERT_TRUE(!gate.IsInFlight()); // token not captured -> expired immediately
    std::cout << "PacedDispatch_TryFireClearSucceeds() complete!" << std::endl;
}

static void PacedDispatch_TryFireInFlightFails()
{
    PacedDispatch gate;
    std::shared_ptr<DispatchToken> liveToken;

    bool first = gate.TryFire([&liveToken](std::shared_ptr<DispatchToken> t) { liveToken = t; });
    ASSERT_TRUE(first == true);
    ASSERT_TRUE(gate.IsInFlight());

    bool second = gate.TryFire([](std::shared_ptr<DispatchToken>) {});
    ASSERT_TRUE(second == false);
    std::cout << "PacedDispatch_TryFireInFlightFails() complete!" << std::endl;
}

static void PacedDispatch_TryFireAfterTokenReleased()
{
    PacedDispatch gate;
    std::shared_ptr<DispatchToken> liveToken;

    gate.TryFire([&liveToken](std::shared_ptr<DispatchToken> t) { liveToken = t; });
    ASSERT_TRUE(gate.IsInFlight());

    liveToken.reset(); // simulate message completion
    ASSERT_TRUE(!gate.IsInFlight());

    bool result = gate.TryFire([](std::shared_ptr<DispatchToken>) {});
    ASSERT_TRUE(result == true);
    std::cout << "PacedDispatch_TryFireAfterTokenReleased() complete!" << std::endl;
}

static void PacedDispatch_Reset()
{
    PacedDispatch gate;
    std::shared_ptr<DispatchToken> liveToken;

    gate.TryFire([&liveToken](std::shared_ptr<DispatchToken> t) { liveToken = t; });
    ASSERT_TRUE(gate.IsInFlight());

    gate.Reset();
    ASSERT_TRUE(!gate.IsInFlight());

    bool result = gate.TryFire([](std::shared_ptr<DispatchToken>) {});
    ASSERT_TRUE(result == true);
    std::cout << "PacedDispatch_Reset() complete!" << std::endl;
}

static void PacedDispatch_PendingFlag()
{
    PacedDispatch gate;
    std::shared_ptr<DispatchToken> liveToken;

    // First fire — succeeds, sets in-flight
    bool first = gate.TryFire([&liveToken](std::shared_ptr<DispatchToken> t) { liveToken = t; });
    ASSERT_TRUE(first == true);
    ASSERT_TRUE(gate.IsInFlight());
    ASSERT_TRUE(!gate.IsPending());

    // Second fire — fails, sets pending
    bool second = gate.TryFire([](std::shared_ptr<DispatchToken>) {});
    ASSERT_TRUE(second == false);
    ASSERT_TRUE(gate.IsPending());

    // Release token
    liveToken.reset();
    ASSERT_TRUE(!gate.IsInFlight());
    ASSERT_TRUE(gate.IsPending());

    // Third fire — succeeds because was pending, clears pending
    bool third = gate.TryFire([](std::shared_ptr<DispatchToken>) {});
    ASSERT_TRUE(third == true);
    ASSERT_TRUE(!gate.IsPending());

    std::cout << "PacedDispatch_PendingFlag() complete!" << std::endl;
}

static void PacedDispatch_OnStuckFires()
{
    PacedDispatch gate;
    std::shared_ptr<DispatchToken> liveToken;
    std::atomic<int> stuckCount{ 0 };
    gate.OnStuck = [&stuckCount]() { stuckCount.fetch_add(1); };

    gate.TryFire([&liveToken](std::shared_ptr<DispatchToken> t) { liveToken = t; });

    // Sleep past the stuck threshold, then trigger with a short stuckTimeout
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    gate.TryFire([](std::shared_ptr<DispatchToken>) {}, std::chrono::milliseconds(1));

    ASSERT_TRUE(stuckCount.load() >= 1);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        count.fetch_add(1);
    }
};

static void TimerDelegate_RawPtr_Dispatches()
{
    TDTarget target;
    auto d = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);
    d();
    ASSERT_TRUE(WaitCount(target.count, 1));
    Drain(s_thread);
    std::cout << "TimerDelegate_RawPtr_Dispatches() complete!" << std::endl;
}

static void TimerDelegate_ConstRawPtr_Dispatches()
{
    std::atomic<int> count{ 0 };
    struct ConstTarget
    {
        std::atomic<int>* pCount;
        void OnTick() const { pCount->fetch_add(1); }
    };
    ConstTarget ct{ &count };
    const ConstTarget* cptr = &ct;

    auto d = MakeTimerDelegate(cptr, &ConstTarget::OnTick, s_thread);
    d();
    ASSERT_TRUE(WaitCount(count, 1));
    Drain(s_thread);
    std::cout << "TimerDelegate_ConstRawPtr_Dispatches() complete!" << std::endl;
}

static void TimerDelegate_AtMostOneInFlight()
{
    TDTarget target;
    auto d = MakeTimerDelegate(&target, &TDTarget::OnTickSlow, s_thread);

    // Fire 10 times in rapid succession. Only the first goes through;
    // the remaining 9 are skipped because the token is alive during the 150ms sleep.
    for (int i = 0; i < 10; i++)
        d();

    ASSERT_TRUE(WaitCount(target.count, 1, std::chrono::milliseconds(3000)));
    Drain(s_thread);
    ASSERT_TRUE(target.count.load() == 1);
    std::cout << "TimerDelegate_AtMostOneInFlight() complete!" << std::endl;
}

static void TimerDelegate_FiresAgainAfterCompletion()
{
    TDTarget target;
    auto d = MakeTimerDelegate(&target, &TDTarget::OnTickSlow, s_thread);

    // First dispatch
    d();
    ASSERT_TRUE(WaitCount(target.count, 1, std::chrono::milliseconds(3000)));
    Drain(s_thread); // ensures token is expired before firing again

    // Second dispatch — token is now free
    d();
    ASSERT_TRUE(WaitCount(target.count, 2, std::chrono::milliseconds(3000)));
    Drain(s_thread);

    ASSERT_TRUE(target.count.load() == 2);
    std::cout << "TimerDelegate_FiresAgainAfterCompletion() complete!" << std::endl;
}

static void TimerDelegate_SharedPtr_Dispatches()
{
    auto target = std::make_shared<TDTarget>();
    auto d = MakeTimerDelegate(target, &TDTarget::OnTick, s_thread);
    d();
    ASSERT_TRUE(WaitCount(target->count, 1));
    Drain(s_thread);
    std::cout << "TimerDelegate_SharedPtr_Dispatches() complete!" << std::endl;
}

static void TimerDelegate_SharedPtr_SkipsDestroyedObject()
{
    auto target = std::make_shared<TDTarget>();
    auto d = MakeTimerDelegate(target, &TDTarget::OnTick, s_thread);

    // First fire — object alive
    d();
    ASSERT_TRUE(WaitCount(target->count, 1));
    Drain(s_thread);
    ASSERT_TRUE(target->count.load() == 1);

    // Destroy object; delegate holds only a weak_ptr
    target.reset();

    // Second fire — weak_ptr.lock() returns null, dispatch silently skipped
    d();
    Drain(s_thread); // no crash expected
    std::cout << "TimerDelegate_SharedPtr_SkipsDestroyedObject() complete!" << std::endl;
}

// =============================================================================
// Integration: TimerDelegate wired to a real Timer
// =============================================================================

static void TimerDelegate_WithTimer_DispatchesToThread()
{
    std::atomic<bool> timerExit{ false };
    std::thread timerThread([&timerExit]() {
        while (!timerExit.load()) {
            Timer::ProcessTimers();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    TDTarget target;
    Timer timer;
    dmq::ScopedConnection conn = timer.OnExpired.Connect(
        MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread));

    timer.Start(std::chrono::milliseconds(20));
    ASSERT_TRUE(WaitCount(target.count, 3, std::chrono::milliseconds(2000)));

    timer.Stop();
    timerExit.store(true);
    timerThread.join();
    Drain(s_thread);

    ASSERT_TRUE(target.count.load() >= 3);
    std::cout << "TimerDelegate_WithTimer_DispatchesToThread() complete!" << std::endl;
}

// =============================================================================
// Test runner
// =============================================================================

void TimerDelegateTests()
{
    s_thread.CreateThread();

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
    TimerDelegate_WithTimer_DispatchesToThread();

    std::cout << "TimerDelegateTests() complete!" << std::endl;
}
