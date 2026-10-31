// Checks that DelegateMQ works when built without DelegateMQ.cmake: one source
// file (src/DelegateMQ.cpp) and options from config/DelegateMQConfig.h.
// Prints "ALL TESTS PASSED" and returns 0 on success.

#include "DelegateMQ.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

// config/DelegateMQConfig.h defines DMQ_ALLOCATOR. If it wasn't found, stop here.
#if !defined(DMQ_ALLOCATOR)
#error "config/DelegateMQConfig.h was not picked up"
#endif

using namespace std::chrono_literals;

static std::atomic<int> g_count{0};
static std::thread::id g_lastThread;

static int Add(int a, int b)
{
    g_lastThread = std::this_thread::get_id();
    return a + b;
}

static void Increment(int n) { g_count += n; }

// Wait up to 2 seconds for a condition set on another thread.
template <class Pred>
static bool WaitFor(Pred pred)
{
    for (int i = 0; i < 200 && !pred(); i++)
        std::this_thread::sleep_for(10ms);
    return pred();
}

static int s_failures = 0;
static void Check(bool ok, const char* name)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        s_failures++;
}

int main()
{
    std::printf("DelegateMQ built from src/DelegateMQ.cpp (no DelegateMQ.cmake)\n");

    dmq::os::Thread worker("Worker");
    worker.CreateThread();

    // 1. Synchronous delegate: runs on the calling thread.
    auto sync = dmq::MakeDelegate(&Add);
    Check(sync(2, 3) == 5 && g_lastThread == std::this_thread::get_id(), "synchronous delegate");

    // 2. Asynchronous delegate, blocking: runs on the worker, returns the result.
    auto asyncWait = dmq::MakeDelegate(&Add, worker, dmq::WAIT_INFINITE);
    Check(asyncWait(20, 22) == 42 && g_lastThread != std::this_thread::get_id(),
          "asynchronous blocking delegate (result from worker thread)");

    // 3. Asynchronous delegate, fire-and-forget.
    g_count = 0;
    auto async = dmq::MakeDelegate(&Increment, worker);
    async(1);
    async(2);
    Check(WaitFor([] { return g_count == 3; }), "asynchronous fire-and-forget delegate");

    // 4. Signal with a slot dispatched to the worker thread.
    g_count = 0;
    dmq::Signal<void(int)> onValue;
    {
        dmq::ScopedConnection conn = onValue.Connect(dmq::MakeDelegate(&Increment, worker));
        onValue(10);
        Check(WaitFor([] { return g_count == 10; }), "signal to worker thread");
    }
    onValue(10);    // disconnected: no effect
    std::this_thread::sleep_for(50ms);
    Check(g_count == 10, "scoped connection disconnects");

    // 5. One-shot timer, callback dispatched to the worker thread.
    g_count = 0;
    dmq::util::Timer timer;
    auto timerConn = timer.OnExpired.Connect(
        dmq::MakeDelegate([]() { Increment(1); }, worker));
    timer.Start(std::chrono::milliseconds(50), true);
    Check(WaitFor([] { dmq::util::Timer::ProcessTimers(); return g_count == 1; }),
          "one-shot timer");
    timer.Stop();

    worker.ExitThread();

    if (s_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d TEST(S) FAILED\n", s_failures);
    return 1;
}
