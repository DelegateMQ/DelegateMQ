/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief An application using DelegateMQ only through the DelegateMQ::DelegateMQ
/// CMake target (see CMakeLists.txt). Checks that the target brings the
/// options set before it was added (DMQ_ALLOCATOR) and that the library it
/// built works: sync, async and async-wait delegates, a signal, a timer.
/// Prints ALL TESTS PASSED and returns 0, or returns 1 on the first failure.

#include "DelegateMQ.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

// Set in CMakeLists.txt before FetchContent_MakeAvailable(); the target
// passes it on to everything that links it.
#if !defined(DMQ_ALLOCATOR)
#error "DMQ_ALLOCATOR was set before adding DelegateMQ but didn't reach this target"
#endif

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

extern int NotLinkedValue();   // not_linked.cpp, in a target without DelegateMQ

static int g_failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAILED: %s (line %d)\n", #cond, __LINE__); g_failures++; } } while (0)

static int Add(int a, int b) { return a + b; }

class Counter
{
public:
    void Increment(int by) { m_count += by; }
    int Get() const { return m_count; }
private:
    std::atomic<int> m_count{ 0 };
};

int main()
{
    std::atomic<bool> timersExit{ false };
    std::thread timerThread([&] {
        while (!timersExit) {
            Timer::ProcessTimers();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    Thread worker("Worker");
    CHECK(worker.CreateThread());

    // Synchronous delegate to a free function
    auto add = MakeDelegate(&Add);
    CHECK(add(2, 3) == 5);

    // Asynchronous delegate to a member function, run on the worker thread
    Counter counter;
    auto increment = MakeDelegate(&counter, &Counter::Increment, worker);
    increment(1);
    increment(2);

    // Async-wait: runs on the worker and returns its result. Messages run in
    // order, so both increments are done when it returns.
    auto addWait = MakeDelegate(&Add, worker, WAIT_INFINITE);
    CHECK(addWait(20, 22) == 42);
    CHECK(counter.Get() == 3);

    // Signal with a scoped connection
    Signal<void(int)> signal;
    int received = 0;
    {
        ScopedConnection conn = signal.Connect(MakeDelegate(std::function<void(int)>([&](int v) { received += v; })));
        signal(5);
    }
    signal(100);   // disconnected: not received
    CHECK(received == 5);

    // One-shot timer, its callback on the worker thread
    Timer timer;
    std::atomic<bool> fired{ false };
    auto onTimer = MakeDelegate(std::function<void()>([&] { fired = worker.IsCurrentThread(); }), worker);
    ScopedConnection timerConn = timer.OnExpired.Connect(onTimer);
    timer.Start(std::chrono::milliseconds(50), true);
    for (int i = 0; i < 200 && !fired; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(fired);
    timer.Stop();

    CHECK(NotLinkedValue() == 42);

    worker.ExitThread();
    timersExit = true;
    timerThread.join();

    if (g_failures > 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("ALL TESTS PASSED\n");
    return 0;
}
