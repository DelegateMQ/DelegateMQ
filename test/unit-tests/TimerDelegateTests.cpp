#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include "extras/util/TimerDelegate.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

static Thread s_thread("TimerDelegateTests");

// Wait until count reaches expected or timeout elapses. Returns true on success.
static bool WaitCount(const std::atomic<int>& cnt, int expected,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (cnt.load() < expected && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return cnt.load() >= expected;
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
    DMQ_ASSERT_TRUE(result == true);
    DMQ_ASSERT_TRUE(fired == true);
    DMQ_ASSERT_TRUE(!gate.IsInFlight()); // token not captured -> expired immediately
    std::cout << "PacedDispatch_TryFireClearSucceeds() complete!" << std::endl;
}

static void PacedDispatch_TryFireInFlightFails()
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

static void PacedDispatch_TryFireAfterTokenReleased()
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

static void PacedDispatch_Reset()
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

static void PacedDispatch_PendingFlag()
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

static void PacedDispatch_OnStuckFires()
{
    PacedDispatch gate;
    std::shared_ptr<DispatchToken> liveToken;
    std::atomic<int> stuckCount{ 0 };
    gate.OnStuck = dmq::MakeDelegate([&stuckCount]() { stuckCount.fetch_add(1); });

    gate.TryFire([&liveToken](std::shared_ptr<DispatchToken> t) { liveToken = t; });

    // Sleep past the stuck threshold, then trigger with a short stuckTimeout
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        count.fetch_add(1);
    }
};

static void TimerDelegate_RawPtr_Dispatches()
{
    TDTarget target;
    auto d = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);
    d();
    DMQ_ASSERT_TRUE(WaitCount(target.count, 1));
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
    DMQ_ASSERT_TRUE(WaitCount(count, 1));
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

    DMQ_ASSERT_TRUE(WaitCount(target.count, 1, std::chrono::milliseconds(3000)));
    Drain(s_thread);
    DMQ_ASSERT_TRUE(target.count.load() == 1);
    std::cout << "TimerDelegate_AtMostOneInFlight() complete!" << std::endl;
}

static void TimerDelegate_FiresAgainAfterCompletion()
{
    TDTarget target;
    auto d = MakeTimerDelegate(&target, &TDTarget::OnTickSlow, s_thread);

    // First dispatch
    d();
    DMQ_ASSERT_TRUE(WaitCount(target.count, 1, std::chrono::milliseconds(3000)));
    Drain(s_thread); // ensures token is expired before firing again

    // Second dispatch — token is now free
    d();
    DMQ_ASSERT_TRUE(WaitCount(target.count, 2, std::chrono::milliseconds(3000)));
    Drain(s_thread);

    DMQ_ASSERT_TRUE(target.count.load() == 2);
    std::cout << "TimerDelegate_FiresAgainAfterCompletion() complete!" << std::endl;
}

static void TimerDelegate_SharedPtr_Dispatches()
{
    auto target = std::make_shared<TDTarget>();
    auto d = MakeTimerDelegate(target, &TDTarget::OnTick, s_thread);
    d();
    DMQ_ASSERT_TRUE(WaitCount(target->count, 1));
    Drain(s_thread);
    std::cout << "TimerDelegate_SharedPtr_Dispatches() complete!" << std::endl;
}

static void TimerDelegate_SharedPtr_SkipsDestroyedObject()
{
    auto target = std::make_shared<TDTarget>();
    auto d = MakeTimerDelegate(target, &TDTarget::OnTick, s_thread);

    // First fire — object alive
    d();
    DMQ_ASSERT_TRUE(WaitCount(target->count, 1));
    Drain(s_thread);
    DMQ_ASSERT_TRUE(target->count.load() == 1);

    // Destroy object; delegate holds only a weak_ptr
    target.reset();

    // Second fire — weak_ptr.lock() returns null, dispatch silently skipped
    d();
    Drain(s_thread); // no crash expected
    std::cout << "TimerDelegate_SharedPtr_SkipsDestroyedObject() complete!" << std::endl;
}

// Holds the worker thread inside a handler until released, so messages
// dispatched meanwhile stay queued.
struct WorkerGate
{
    std::atomic<bool> entered{ false };
    std::atomic<bool> release{ false };
    void Block()
    {
        entered.store(true);
        while (!release.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
};

// Regression: the queued tick used to call back into the TimerDelegate through a
// raw `this`. Disconnecting deletes the Signal's TimerDelegate clone, so a tick
// still in the queue then ran InvokeTarget() on freed memory. A disconnected
// TimerDelegate must not invoke its target, even for a tick already queued.
static void TimerDelegate_DisconnectWhileInFlight_NoCallback()
{
    WorkerGate gate;
    TDTarget target;

    dmq::MakeDelegate(&gate, &WorkerGate::Block, s_thread)();
    while (!gate.entered.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    {
        dmq::Signal<void()> onExpired;
        dmq::ScopedConnection conn = onExpired.Connect(
            MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread));

        onExpired();                                  // tick queued behind the gate
        DMQ_ASSERT_TRUE(s_thread.GetQueueSize() == 1);

        conn.Disconnect();                            // deletes the TimerDelegate
    }

    gate.release.store(true);
    Drain(s_thread);

    DMQ_ASSERT_TRUE(target.count.load() == 0);
    std::cout << "TimerDelegate_DisconnectWhileInFlight_NoCallback() complete!" << std::endl;
}

// Same hazard for a TimerDelegate destroyed directly (not via a Signal), and
// with a shared_ptr target that is still alive.
static void TimerDelegate_DestroyedWhileInFlight_NoCallback()
{
    WorkerGate gate;
    auto target = std::make_shared<TDTarget>();

    dmq::MakeDelegate(&gate, &WorkerGate::Block, s_thread)();
    while (!gate.entered.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    {
        auto d = MakeTimerDelegate(target, &TDTarget::OnTick, s_thread);
        d();
        DMQ_ASSERT_TRUE(s_thread.GetQueueSize() == 1);
    }                                                 // d destroyed, tick still queued

    gate.release.store(true);
    Drain(s_thread);

    DMQ_ASSERT_TRUE(target->count.load() == 0);
    std::cout << "TimerDelegate_DestroyedWhileInFlight_NoCallback() complete!" << std::endl;
}

// Occupy s_thread until gate.release is set.
static void BlockWorker(WorkerGate& gate)
{
    dmq::MakeDelegate(&gate, &WorkerGate::Block, s_thread)();
    while (!gate.entered.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

// Copy assignment must not share state: b = a, then destroying a cancels a's
// queued tick, and b's own first tick isn't skipped as "in flight".
static void TimerDelegate_CopyAssign_DoesNotShareState()
{
    WorkerGate gate;
    TDTarget target;
    auto b = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);

    BlockWorker(gate);
    {
        auto a = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);
        a();                                          // a's tick queued
        DMQ_ASSERT_TRUE(s_thread.GetQueueSize() == 1);
        b = a;
    }                                                 // a destroyed: its tick is cancelled

    b();                                              // b's own tick: not blocked by a's gate
    DMQ_ASSERT_TRUE(s_thread.GetQueueSize() == 2);

    gate.release.store(true);
    Drain(s_thread);
    DMQ_ASSERT_TRUE(target.count.load() == 1);        // only b's tick ran
    std::cout << "TimerDelegate_CopyAssign_DoesNotShareState() complete!" << std::endl;
}

// Clear() cancels a queued tick and resets the gate.
static void TimerDelegate_Clear_CancelsQueuedTick()
{
    WorkerGate gate;
    TDTarget target;
    auto d = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);

    BlockWorker(gate);
    d();
    DMQ_ASSERT_TRUE(s_thread.GetQueueSize() == 1);
    d.Clear();
    DMQ_ASSERT_TRUE(d.Empty());
    d();                                              // empty: no dispatch, no crash
    DMQ_ASSERT_TRUE(s_thread.GetQueueSize() == 1);

    gate.release.store(true);
    Drain(s_thread);
    DMQ_ASSERT_TRUE(target.count.load() == 0);
    std::cout << "TimerDelegate_Clear_CancelsQueuedTick() complete!" << std::endl;
}

// A move transfers the queued tick to the destination; destroying the
// moved-from source doesn't cancel it.
static void TimerDelegate_Move_TransfersQueuedTick()
{
    WorkerGate gate;
    TDTarget target;
    auto dst = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);

    BlockWorker(gate);
    {
        auto src = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);
        src();
        DMQ_ASSERT_TRUE(s_thread.GetQueueSize() == 1);
        dst = std::move(src);
        DMQ_ASSERT_TRUE(src.Empty());
        dst();                                        // still in flight: skipped
        DMQ_ASSERT_TRUE(s_thread.GetQueueSize() == 1);
    }

    gate.release.store(true);
    Drain(s_thread);
    DMQ_ASSERT_TRUE(target.count.load() == 1);
    std::cout << "TimerDelegate_Move_TransfersQueuedTick() complete!" << std::endl;
}

// =============================================================================
// TimerDelegate::Equal() / Empty() / nullptr comparisons
// =============================================================================

static void FreeFuncForEqualityTests() {}

static void TimerDelegate_Equal_SameUnderlyingAndThread()
{
    TDTarget target;
    auto d1 = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);
    auto d2 = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);
    DMQ_ASSERT_TRUE(d1.Equal(d2));
    DMQ_ASSERT_TRUE(d2.Equal(d1));
    std::cout << "TimerDelegate_Equal_SameUnderlyingAndThread() complete!" << std::endl;
}

static void TimerDelegate_Equal_DifferentTargetObject()
{
    TDTarget target1;
    TDTarget target2;
    auto d1 = MakeTimerDelegate(&target1, &TDTarget::OnTick, s_thread);
    auto d2 = MakeTimerDelegate(&target2, &TDTarget::OnTick, s_thread);
    DMQ_ASSERT_TRUE(!d1.Equal(d2));
    std::cout << "TimerDelegate_Equal_DifferentTargetObject() complete!" << std::endl;
}

static void TimerDelegate_Equal_DifferentThread()
{
    static Thread otherThread("TimerDelegateTests_OtherThread");
    otherThread.CreateThread();

    TDTarget target;
    auto d1 = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);
    auto d2 = MakeTimerDelegate(&target, &TDTarget::OnTick, otherThread);
    DMQ_ASSERT_TRUE(!d1.Equal(d2));

    otherThread.ExitThread();
    std::cout << "TimerDelegate_Equal_DifferentThread() complete!" << std::endl;
}

static void TimerDelegate_Equal_DifferentDelegateType()
{
    // rhs isn't a TimerDelegate at all -- exercises dynamic_cast failing
    // safely and Equal() returning false rather than throwing/crashing.
    TDTarget target;
    auto d1 = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);
    auto other = dmq::MakeDelegate(&FreeFuncForEqualityTests);
    DMQ_ASSERT_TRUE(!d1.Equal(other));
    std::cout << "TimerDelegate_Equal_DifferentDelegateType() complete!" << std::endl;
}

static void TimerDelegate_Equal_BothEmpty()
{
    TDTarget target;
    auto d1 = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);
    auto d2 = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);
    d1.Clear();
    d2.Clear();
    DMQ_ASSERT_TRUE(d1.Empty());
    DMQ_ASSERT_TRUE(d2.Empty());
    DMQ_ASSERT_TRUE(d1.Equal(d2));
    std::cout << "TimerDelegate_Equal_BothEmpty() complete!" << std::endl;
}

static void TimerDelegate_EmptyAndNullptrComparisons()
{
    TDTarget target;
    auto d = MakeTimerDelegate(&target, &TDTarget::OnTick, s_thread);
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
    DMQ_ASSERT_TRUE(WaitCount(target.count, 3, std::chrono::milliseconds(2000)));

    timer.Stop();
    timerExit.store(true);
    timerThread.join();
    Drain(s_thread);

    DMQ_ASSERT_TRUE(target.count.load() >= 3);
    std::cout << "TimerDelegate_WithTimer_DispatchesToThread() complete!" << std::endl;
}

static void Timer_ProcessTimers_DrainsMultiplePasses()
{
    // More than MAX_TIMER_EXPIRED timers all due at once — a single
    // ProcessTimers() call must drain every batch, not just the first, and
    // must not hard-fault (see Timer::ProcessTimers()'s multi-pass restructure,
    // mirroring TransportMonitor::Process()'s identical batch-cap handling).
    const size_t timerCount = dmq::MAX_TIMER_EXPIRED + 5;

    std::vector<std::unique_ptr<Timer>> timers;
    std::vector<dmq::ScopedConnection> conns;
    std::atomic<int> fireCount{ 0 };

    for (size_t i = 0; i < timerCount; ++i) {
        auto timer = std::make_unique<Timer>();
        conns.push_back(timer->OnExpired.Connect(
            dmq::MakeDelegate([&fireCount]() { fireCount.fetch_add(1); })));
        timer->Start(std::chrono::milliseconds(1), /*once=*/true);
        timers.push_back(std::move(timer));
    }

    // Let every one-shot timer become due.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // OnExpired has no thread argument, so callbacks fire synchronously inside
    // this call — one ProcessTimers() call must account for all of them.
    Timer::ProcessTimers();

    DMQ_ASSERT_TRUE(fireCount.load() == static_cast<int>(timerCount));
    std::cout << "Timer_ProcessTimers_DrainsMultiplePasses() complete!" << std::endl;
}

static void Timer_DestroyedDuringProcessTimers()
{
    // ProcessTimers() finds expired timers under its critical section but copies
    // their subscribers after leaving it (that copy takes OS mutexes). Timers
    // destroyed on other threads in that window must not be touched afterwards.
    // A use-after-free here shows up under the sanitizer build.
    std::atomic<bool> exit{ false };
    std::atomic<int> fireCount{ 0 };

    std::thread processor([&exit]() {
        while (!exit.load())
            Timer::ProcessTimers();
    });

    std::vector<std::thread> workers;
    for (int w = 0; w < 4; ++w) {
        workers.emplace_back([&exit, &fireCount]() {
            while (!exit.load()) {
                auto timer = std::make_unique<Timer>();
                dmq::ScopedConnection conn = timer->OnExpired.Connect(
                    dmq::MakeDelegate([&fireCount]() { fireCount.fetch_add(1); }));
                timer->Start(std::chrono::milliseconds(1));
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                timer.reset();    // destroy while still connected and due
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    exit.store(true);
    for (auto& t : workers)
        t.join();
    processor.join();

    DMQ_ASSERT_TRUE(fireCount.load() > 0);
    std::cout << "Timer_DestroyedDuringProcessTimers() complete!" << std::endl;
}

static void Timer_CallbackWaitsForOtherThreadToDestroyTimer()
{
    // The lock that keeps timers alive during ProcessTimers() must be released
    // before callbacks run. Here a callback waits for another thread to destroy
    // a timer; if the lock were still held, ~Timer() would block on it and the
    // wait would time out.
    std::atomic<bool> destroyed{ false };
    std::atomic<bool> callbackDone{ false };
    auto victim = std::make_unique<Timer>();

    Timer trigger;
    dmq::ScopedConnection conn = trigger.OnExpired.Connect(dmq::MakeDelegate(
        [&victim, &destroyed, &callbackDone]() {
            if (callbackDone.exchange(true))
                return;
            std::thread killer([&victim, &destroyed]() {
                victim.reset();
                destroyed.store(true);
            });
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!destroyed.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            DMQ_ASSERT_TRUE(destroyed.load());    // faults here instead of hanging below
            killer.join();
        }));

    trigger.Start(std::chrono::milliseconds(1), /*once=*/true);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Timer::ProcessTimers();

    // The test runner's own ProcessTimers() thread may have fired it first.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!destroyed.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    DMQ_ASSERT_TRUE(destroyed.load());
    std::cout << "Timer_CallbackWaitsForOtherThreadToDestroyTimer() complete!" << std::endl;
}

static void Timer_FallenBehind_DoesNotFireBackToBack()
{
    // A periodic timer that misses several periods must skip them and expire one
    // full period later, not fire again on the very next ProcessTimers() call.
    // Relies on the test runner's ProcessTimers() thread (5 ms loop) being the
    // only caller: the blocker's callback stalls it long enough for the victim
    // to fall more than a period behind.
    //
    // Waits for a number of fires rather than counting them in a fixed window:
    // on a loaded machine the runner thread can run late, which is not a
    // failure. The check that matters is the gap between consecutive fires.
    const auto period = std::chrono::milliseconds(200);

    std::mutex mtx;
    std::vector<std::chrono::steady_clock::time_point> fires;

    Timer blocker;
    dmq::ScopedConnection blockerConn = blocker.OnExpired.Connect(dmq::MakeDelegate([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }));

    Timer victim;
    dmq::ScopedConnection victimConn = victim.OnExpired.Connect(dmq::MakeDelegate([&mtx, &fires]() {
        std::lock_guard<std::mutex> lock(mtx);
        fires.push_back(std::chrono::steady_clock::now());
    }));

    blocker.Start(std::chrono::milliseconds(20), /*once=*/true);
    victim.Start(period);

    // Three fires: the late one after the stall, and two more, so the gap right
    // after the stall (where a back-to-back fire would show) is always checked.
    const size_t wanted = 3;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (fires.size() >= wanted)
                break;
        }
        DMQ_ASSERT_TRUE(std::chrono::steady_clock::now() < deadline);   // victim stopped firing
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    victim.Stop();

    std::lock_guard<std::mutex> lock(mtx);
    DMQ_ASSERT_TRUE(fires.size() >= wanted);
    for (size_t i = 1; i < fires.size(); ++i)
        DMQ_ASSERT_TRUE(fires[i] - fires[i - 1] >= std::chrono::milliseconds(50));
    std::cout << "Timer_FallenBehind_DoesNotFireBackToBack() complete!" << std::endl;
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
    TimerDelegate_DisconnectWhileInFlight_NoCallback();
    TimerDelegate_DestroyedWhileInFlight_NoCallback();
    TimerDelegate_CopyAssign_DoesNotShareState();
    TimerDelegate_Clear_CancelsQueuedTick();
    TimerDelegate_Move_TransfersQueuedTick();

    TimerDelegate_Equal_SameUnderlyingAndThread();
    TimerDelegate_Equal_DifferentTargetObject();
    TimerDelegate_Equal_DifferentThread();
    TimerDelegate_Equal_DifferentDelegateType();
    TimerDelegate_Equal_BothEmpty();
    TimerDelegate_EmptyAndNullptrComparisons();

    TimerDelegate_WithTimer_DispatchesToThread();
    Timer_ProcessTimers_DrainsMultiplePasses();
    Timer_DestroyedDuringProcessTimers();
    Timer_CallbackWaitsForOtherThreadToDestroyTimer();
    Timer_FallenBehind_DoesNotFireBackToBack();

    std::cout << "TimerDelegateTests() complete!" << std::endl;
}
