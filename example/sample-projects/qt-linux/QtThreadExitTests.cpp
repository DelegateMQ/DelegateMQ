/// @file QtThreadExitTests.cpp
/// @brief Qt port: ExitThread() while a producer is blocked on a full
/// FullPolicy::TIMEOUT queue, then re-creating the thread.
///
/// Regression tests for QtThread::ExitThread():
/// - A producer blocked waiting for queue space returns when the thread exits,
///   instead of waiting out the dispatch timeout or queuing its message behind
///   the quit, where it would never run.
/// - An async-wait (WAIT_INFINITE) producer blocked the same way returns with
///   no value instead of hanging.
/// - A re-created thread starts with a queue count of 0 and works normally.
/// - Run under AddressSanitizer, the exits also cover the Worker being deleted
///   by its own thread before ExitThread() returns.

#include "DelegateMQ.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

using namespace dmq;
using namespace dmq::os;
using namespace std::chrono;

namespace {

// Long enough that a producer still blocked when the test checks would show
// up as a timing failure, not a pass.
constexpr auto DISPATCH_TIMEOUT = seconds(10);
constexpr auto RETURN_LIMIT = milliseconds(2000);
constexpr int ITERATIONS = 10;

std::atomic<int> g_producerRan{ 0 };

void ProducerMsg() { g_producerRan++; }
int Add(int a, int b) { g_producerRan++; return a + b; }

// Occupy the worker with a slow message, then fill the single queue slot.
void FillQueue(Thread& t)
{
    MakeDelegate(+[]() { std::this_thread::sleep_for(milliseconds(200)); }, t)();
    std::this_thread::sleep_for(milliseconds(20));
    MakeDelegate(+[]() {}, t)();
}

// Producer blocked on a full queue when ExitThread() runs. invoke() is the
// blocked dispatch; it runs on its own thread.
template <typename Invoke>
void ExitWithBlockedProducer(Thread& t, int iteration, Invoke invoke)
{
    t.CreateThread();
    DMQ_ASSERT_TRUE(t.GetQueueSize() == 0);

    FillQueue(t);

    std::atomic<bool> returned{ false };
    std::thread producer([&]() {
        invoke();
        returned = true;
    });

    // Vary the timing so ExitThread() lands at different points of the wait
    std::this_thread::sleep_for(milliseconds(30 + (iteration % 7) * 10));
    DMQ_ASSERT_TRUE(!returned.load());  // queue full: still blocked

    auto start = steady_clock::now();
    t.ExitThread();
    producer.join();
    auto elapsed = steady_clock::now() - start;

    // Returned with the exit, not after DISPATCH_TIMEOUT
    DMQ_ASSERT_TRUE(elapsed < RETURN_LIMIT);

    // Dropped, not queued behind the quit: it never ran and isn't counted
    DMQ_ASSERT_TRUE(g_producerRan.exchange(0) == 0);
    DMQ_ASSERT_TRUE(t.GetQueueSize() == 0);

    // The re-created thread works normally
    t.CreateThread();
    DMQ_ASSERT_TRUE(t.GetQueueSize() == 0);
    auto add = MakeDelegate(&Add, t, WAIT_INFINITE);
    auto sum = add.AsyncInvoke(2, 3);
    DMQ_ASSERT_TRUE(sum.has_value() && sum.value() == 5);
    g_producerRan = 0;
    t.ExitThread();
}

void ExitThread_ReleasesBlockedProducer()
{
    Thread t("ExitBlockedProducer", 1, FullPolicy::TIMEOUT, DISPATCH_TIMEOUT);
    for (int i = 0; i < ITERATIONS; i++)
        ExitWithBlockedProducer(t, i, [&t]() { MakeDelegate(&ProducerMsg, t)(); });
    std::cout << "ExitThread_ReleasesBlockedProducer() complete!" << std::endl;
}

void ExitThread_ReleasesBlockedAsyncWaitProducer()
{
    Thread t("ExitBlockedWaitProducer", 1, FullPolicy::TIMEOUT, DISPATCH_TIMEOUT);
    for (int i = 0; i < ITERATIONS; i++)
    {
        ExitWithBlockedProducer(t, i, [&t]() {
            auto add = MakeDelegate(&Add, t, WAIT_INFINITE);
            auto sum = add.AsyncInvoke(1, 1);
            DMQ_ASSERT_TRUE(!sum.has_value());  // dropped: no return value
        });
    }
    std::cout << "ExitThread_ReleasesBlockedAsyncWaitProducer() complete!" << std::endl;
}

} // namespace

void QtThreadExitTests()
{
    ExitThread_ReleasesBlockedProducer();
    ExitThread_ReleasesBlockedAsyncWaitProducer();
    std::cout << "QtThreadExitTests() complete!" << std::endl;
}
