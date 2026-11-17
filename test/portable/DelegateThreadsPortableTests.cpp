/// @file DelegateThreadsPortableTests.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
///
/// @brief Cross-thread delegate dispatch and FullPolicy tests on any thread
/// port, from test/unit-tests/DelegateThreadsTests.cpp. One copy for every OS
/// port sample (see PortableTests.h); what differs per port comes in
/// PortableTests::Options.
///
/// Portable on purpose, because on an RTOS (and on its host simulation) the
/// std:: equivalents misbehave:
/// - dmq::Mutex, not std::mutex. On Zephyr's native_sim only one Zephyr
///   thread really runs at a time (a token passed between host pthreads). A
///   thread sleeping (dmq::ThisThread::sleep_for) while holding a std::mutex
///   keeps the host lock for the whole sleep; a second thread then blocks in a
///   futex wait the Zephyr scheduler can't see, and nothing hands the token
///   back: deadlock. dmq::Mutex is the kernel's own mutex, so blocking on it
///   yields.
/// - dmq::ThisThread::sleep_for, not std::this_thread: a host sleep doesn't
///   yield to the RTOS scheduler.
/// - No std::thread: a host thread the RTOS doesn't know must not call its
///   queue APIs (blocking on a full queue suspends whichever RTOS thread is
///   current instead).
/// - Threads are constructed on first use, not as file-scope statics: a
///   static's constructor runs before main(), before the RTOS scheduler or
///   heap is up (FreeRTOS, ThreadX, Zephyr).
/// - dmq::Clock, not std::chrono::steady_clock (no clock behind it on some
///   embedded C libraries, e.g. Zephyr's newlib on Arm), and no
///   std::random_device (not on NuttX).

#include "PortableTests.h"
#include "StartGatePortableTest.h"
#include <iostream>
#include <random>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <functional>
#include <memory>

using namespace dmq;
using namespace dmq::os;

namespace {

PortableTests::Options s_options;

Thread& workerThread1()
{
    static Thread t("DelegateThreads1Tests");
    return t;
}
Thread& workerThread2()
{
    static Thread t("DelegateThreads2Tests");
    return t;
}

// Constructed on first use too: on FreeRTOS a dmq::Mutex allocates from the
// FreeRTOS heap, which main() sets up after static initialization.
dmq::Mutex& TestLock()
{
    static dmq::Mutex lock;
    return lock;
}
const int LOOPS = 10;
const int CNT_MAX = 7;
std::atomic<int> callerCnt[CNT_MAX];

// Increased timeout to prevent flaky/busy tests in Debug/CI environments
const std::chrono::milliseconds TEST_TIMEOUT(5000);

void SleepMs(int ms) { dmq::ThisThread::sleep_for(std::chrono::milliseconds(ms)); }

void LowerPriority(Thread& thread)
{
    if (s_options.lowerPriority)
        s_options.lowerPriority(thread);
}

/// The queue capacity a Thread gets with maxQueueSize 0 on this port.
size_t DefaultQueueCapacity()
{
#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_POSIX) || defined(DMQ_THREAD_QT)
    return dmq::THREAD_DESKTOP_QUEUE_SIZE;
#else
    return dmq::DEFAULT_QUEUE_SIZE;
#endif
}

void Wait()
{
    while (workerThread1().GetQueueSize() != 0 || workerThread2().GetQueueSize() != 0)
        SleepMs(10);
    SleepMs(100);
}

std::chrono::milliseconds getRandomTime()
{
    // 0-10 ms. Seeded from dmq::Clock: std::random_device isn't on every port.
    // Static: std::mt19937 is ~2.5 KB, too much for a small RTOS stack.
    static std::mt19937 gen(static_cast<unsigned>(dmq::Clock::now().time_since_epoch().count()));
    static std::uniform_int_distribution<> dis(0, 10);
    return std::chrono::milliseconds(dis(gen));
}

void FreeThreadSafe(std::chrono::milliseconds delay, int idx)
{
    const dmq::LockGuard<dmq::Mutex> lock(TestLock());
    callerCnt[idx]++;
    dmq::ThisThread::sleep_for(delay);
}

std::function<void(std::chrono::milliseconds, int)> LambdaThreadSafe = [](std::chrono::milliseconds delay, int idx)
    {
        const dmq::LockGuard<dmq::Mutex> lock(TestLock());
        callerCnt[idx]++;
        dmq::ThisThread::sleep_for(delay);
    };

class TestClass
{
public:
    void MemberThreadSafe(std::chrono::milliseconds delay, int idx)
    {
        const dmq::LockGuard<dmq::Mutex> lock(TestLock());
        callerCnt[idx]++;
        dmq::ThisThread::sleep_for(delay);
    }
};

/// The four dispatch tests differ only in the delegate target: each fires
/// synchronous, asynchronous and asynchronous-blocking delegates to both
/// worker threads, alone and through a MulticastDelegateSafe, and checks every
/// call arrived exactly once.
template <typename MakeTarget>
void DispatchTest(const char* name, MakeTarget make)
{
    for (auto& c : callerCnt) c.store(0, std::memory_order_relaxed);

    auto delegateSync1 = make();
    auto delegateSync2 = make();
    auto delegateAsync1 = make(workerThread1());
    auto delegateAsync2 = make(workerThread2());
    auto delegateAsyncWait1 = make(workerThread1(), TEST_TIMEOUT);
    auto delegateAsyncWait2 = make(workerThread2(), TEST_TIMEOUT);

    MulticastDelegateSafe<void(std::chrono::milliseconds, int)> container;
    container += delegateSync1;
    container += delegateSync2;
    container += delegateAsync1;
    container += delegateAsync2;
    container += delegateAsyncWait1;
    container += delegateAsyncWait2;

    int cnt = 0;
    int cnt2 = 0;
    while (cnt++ < LOOPS)
    {
        delegateSync1(getRandomTime(), 0);
        delegateSync2(getRandomTime(), 1);

        auto retVal1 = delegateAsyncWait1.AsyncInvoke(getRandomTime(), 4);
        DMQ_ASSERT_TRUE(retVal1.has_value());
        auto retVal2 = delegateAsyncWait2.AsyncInvoke(getRandomTime(), 5);
        DMQ_ASSERT_TRUE(retVal2.has_value());

        while (cnt2++ < 5)
        {
            delegateAsync1(getRandomTime(), 2);
            delegateAsync2(getRandomTime(), 3);
        }
        cnt2 = 0;
        container(getRandomTime(), 6);
    }

    Wait();

    DMQ_ASSERT_TRUE(callerCnt[0] == LOOPS);
    DMQ_ASSERT_TRUE(callerCnt[1] == LOOPS);
    DMQ_ASSERT_TRUE(callerCnt[2] == LOOPS * 5);
    DMQ_ASSERT_TRUE(callerCnt[3] == LOOPS * 5);
    DMQ_ASSERT_TRUE(callerCnt[4] == LOOPS);
    DMQ_ASSERT_TRUE(callerCnt[5] == LOOPS);
    DMQ_ASSERT_TRUE(callerCnt[6] == LOOPS * 6);
    std::cout << name << "() complete!" << std::endl;
}

void FreeTests()
{
    DispatchTest("FreeTests", [](auto&&... args) { return MakeDelegate(&FreeThreadSafe, args...); });
}

void MemberTests()
{
    TestClass testClass;
    DispatchTest("MemberTests", [&testClass](auto&&... args) {
        return MakeDelegate(&testClass, &TestClass::MemberThreadSafe, args...); });
}

void MemberSpTests()
{
    auto testClass = std::make_shared<TestClass>();
    DispatchTest("MemberSpTests", [&testClass](auto&&... args) {
        return MakeDelegate(testClass, &TestClass::MemberThreadSafe, args...); });
}

void FunctionTests()
{
    DispatchTest("FunctionTests", [](auto&&... args) { return MakeDelegate(LambdaThreadSafe, args...); });
}

// ---------------------------------------------------------------------------
// FullPolicy tests
// ---------------------------------------------------------------------------

/// Wait for a thread's queue to empty, then for its last message to finish.
void Drain(Thread& thread)
{
    while (thread.GetQueueSize() != 0)
        SleepMs(5);
    SleepMs(50);
}

// DROP policy: flooding a full queue drops messages; publisher is never stalled.
void FullPolicy_Drop_DropsWhenFull()
{
    // Queue holds 3 messages. Consumer sleeps 50ms per message so it drains slowly.
    // We fire 10 messages as fast as possible; some must be dropped.
    Thread dropThread("DropThread", 3, FullPolicy::DROP);
    LowerPriority(dropThread);
    dropThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };

    auto slowConsumer = [&deliveredCount]() {
        SleepMs(50);
        deliveredCount++;
    };

    auto start = dmq::Clock::now();

    for (int i = 0; i < 10; i++)
        MakeDelegate(slowConsumer, dropThread)(/* no args */);

    auto elapsed = dmq::Clock::now() - start;

    // Publisher must NOT have blocked — posting 10 messages should finish well
    // under the time it would take to drain even one slot (50ms).
    DMQ_ASSERT_TRUE(elapsed < std::chrono::milliseconds(30));

    // Let the queue drain fully
    SleepMs(300);

    // With a queue depth of 3 and 10 rapid-fire posts, at least some were dropped.
    // Exactly 3 might be delivered (the ones that fit) but we allow a little slack
    // for timing; the invariant is: delivered < 10.
    DMQ_ASSERT_TRUE(deliveredCount < 10);
    DMQ_ASSERT_TRUE(deliveredCount > 0);

    dropThread.ExitThread();
    std::cout << "FullPolicy_Drop_DropsWhenFull() complete! (delivered " << deliveredCount << "/10)" << std::endl;
}

// DROP policy: if the queue never fills, every message is delivered.
void FullPolicy_Drop_DeliversAllWhenBelowLimit()
{
    Thread dropThread("DropBelowLimitThread", 20, FullPolicy::DROP);
    dropThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };
    const int SEND_COUNT = 10;

    for (int i = 0; i < SEND_COUNT; i++)
        MakeDelegate([&deliveredCount]() { deliveredCount++; }, dropThread)();

    Drain(dropThread);
    DMQ_ASSERT_TRUE(deliveredCount == SEND_COUNT);

    dropThread.ExitThread();
    std::cout << "FullPolicy_Drop_DeliversAllWhenBelowLimit() complete!" << std::endl;
}

// DROP policy: the dropped handler is called for each dropped message, with
// the queue depth at the time.
void FullPolicy_Drop_NotifiesDroppedHandler()
{
    Thread dropThread("DropNotifyThread", 3, FullPolicy::DROP);
    std::atomic<int> droppedCount{ 0 };
    std::atomic<size_t> lastDroppedDepth{ 0 };
    dropThread.SetDroppedHandler(MakeDelegate(std::function<void(size_t)>([&](size_t depth) {
        droppedCount++;
        lastDroppedDepth = depth;
        })));
    LowerPriority(dropThread);
    dropThread.CreateThread();

    auto slowConsumer = []() { SleepMs(50); };
    for (int i = 0; i < 10; i++)
        MakeDelegate(slowConsumer, dropThread)();

    SleepMs(300);
    DMQ_ASSERT_TRUE(droppedCount > 0);
    DMQ_ASSERT_TRUE(droppedCount < 10);
    DMQ_ASSERT_TRUE(lastDroppedDepth == 3);

    dropThread.ExitThread();
    std::cout << "FullPolicy_Drop_NotifiesDroppedHandler() complete! (dropped " << droppedCount << "/10)" << std::endl;
}

// TIMEOUT policy: all messages are delivered even when the queue is flooded (within timeout).
void FullPolicy_Timeout_DeliversAll()
{
    Thread timeoutThread("TimeoutThread", 3, FullPolicy::TIMEOUT);
    timeoutThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };
    const int SEND_COUNT = 10;

    // Posts from this thread: TIMEOUT blocks it until a slot opens.
    for (int i = 0; i < SEND_COUNT; i++)
        MakeDelegate([&deliveredCount]() { deliveredCount++; }, timeoutThread)();

    Drain(timeoutThread);
    DMQ_ASSERT_TRUE(deliveredCount == SEND_COUNT);

    timeoutThread.ExitThread();
    std::cout << "FullPolicy_Timeout_DeliversAll() complete!" << std::endl;
}

// TIMEOUT policy: a message that can't be queued within the dispatch timeout
// is dropped, and the dropped handler is called once for it.
void FullPolicy_Timeout_NotifiesDroppedHandler()
{
    Thread timeoutThread("TimeoutNotifyThread", 1, FullPolicy::TIMEOUT, std::chrono::milliseconds(50));
    std::atomic<int> droppedCount{ 0 };
    timeoutThread.SetDroppedHandler(MakeDelegate(std::function<void(size_t)>([&](size_t) {
        droppedCount++;
        })));
    timeoutThread.CreateThread();

    // The consumer is busy for 500 ms; the 1-slot queue takes one more message,
    // and the next waits 50 ms for a slot, then is dropped
    MakeDelegate([]() { SleepMs(500); }, timeoutThread)();
    SleepMs(20);
    MakeDelegate([]() {}, timeoutThread)();
    MakeDelegate([]() {}, timeoutThread)();

    SleepMs(600);
    DMQ_ASSERT_TRUE(droppedCount == 1);

    timeoutThread.ExitThread();
    std::cout << "FullPolicy_Timeout_NotifiesDroppedHandler() complete!" << std::endl;
}

// Default constructor (no policy arg) behaves as FAULT.
void FullPolicy_DefaultIsFault()
{
    // maxQueueSize set, no FullPolicy arg — must default to FAULT.
    // We stay within the limit (3 messages) to avoid triggering the fault handler
    // which would terminate the test process.
    Thread defaultThread("DefaultPolicyThread", 5);
    defaultThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };
    const int SEND_COUNT = 3;

    for (int i = 0; i < SEND_COUNT; i++)
        MakeDelegate([&deliveredCount]() { deliveredCount++; }, defaultThread)();

    Drain(defaultThread);
    DMQ_ASSERT_TRUE(deliveredCount == SEND_COUNT);

    defaultThread.ExitThread();
    std::cout << "FullPolicy_DefaultIsFault() complete!" << std::endl;
}

// FAULT policy: verify it works as expected when not full.
// NOTE: We cannot easily test the "Full" case because it terminates the application.
void FullPolicy_Fault_WorksWhenNotFull()
{
    Thread faultThread("FaultThread", 10, FullPolicy::FAULT);
    faultThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };
    for (int i = 0; i < 5; i++)
        MakeDelegate([&deliveredCount]() { deliveredCount++; }, faultThread)();

    Drain(faultThread);
    DMQ_ASSERT_TRUE(deliveredCount == 5);

    faultThread.ExitThread();
    std::cout << "FullPolicy_Fault_WorksWhenNotFull() complete!" << std::endl;
}

// maxQueueSize 0 means "this port's default capacity" (DefaultQueueCapacity():
// 1000 on the desktop ports, 20 on the RTOS ports), not unlimited. Messages
// within it are all delivered. Kept 5 below capacity: on a cooperative
// scheduler (Zephyr native_sim) a burst of posts completes before the
// consumer runs once.
void FullPolicy_DefaultQueueSize_DeliversAll()
{
    Thread defaultSizeThread("DefaultQueueSizeThread", 0, FullPolicy::DROP);
    defaultSizeThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };
    const int SEND_COUNT = static_cast<int>(std::min<size_t>(50, DefaultQueueCapacity() - 5));

    for (int i = 0; i < SEND_COUNT; i++)
        MakeDelegate([&deliveredCount]() { deliveredCount++; }, defaultSizeThread)();

    Drain(defaultSizeThread);
    DMQ_ASSERT_TRUE(deliveredCount == SEND_COUNT);

    defaultSizeThread.ExitThread();
    std::cout << "FullPolicy_DefaultQueueSize_DeliversAll() complete!" << std::endl;
}

// maxQueueSize 0 still caps the queue: flooding past the default capacity drops.
void FullPolicy_DefaultQueueSize_StillCapsUnderFlood()
{
    Thread floodThread("FloodDefaultQueueSizeThread", 0, FullPolicy::DROP);
    std::atomic<int> droppedCount{ 0 };
    floodThread.SetDroppedHandler(MakeDelegate(std::function<void(size_t)>([&](size_t) {
        droppedCount++;
        })));
    LowerPriority(floodThread);
    floodThread.CreateThread();

    auto slowConsumer = []() { SleepMs(2); };
    const int SEND_COUNT = static_cast<int>(DefaultQueueCapacity()) + 100;
    for (int i = 0; i < SEND_COUNT; i++)
        MakeDelegate(slowConsumer, floodThread)();

    DMQ_ASSERT_TRUE(droppedCount > 0);

    floodThread.ExitThread();
    std::cout << "FullPolicy_DefaultQueueSize_StillCapsUnderFlood() complete! (dropped "
              << droppedCount << "/" << SEND_COUNT << ")" << std::endl;
}

void ThreadFullPolicyTests()
{
    FullPolicy_Drop_DropsWhenFull();
    FullPolicy_Drop_DeliversAllWhenBelowLimit();
    FullPolicy_Drop_NotifiesDroppedHandler();
    FullPolicy_Timeout_DeliversAll();
    FullPolicy_Timeout_NotifiesDroppedHandler();
    FullPolicy_DefaultIsFault();
    FullPolicy_Fault_WorksWhenNotFull();
    FullPolicy_DefaultQueueSize_DeliversAll();
    FullPolicy_DefaultQueueSize_StillCapsUnderFlood();
}

} // namespace

namespace PortableTests {

void DelegateThreadsTests(const Options& options)
{
    s_options = options;

    // The dispatch tests keep both worker threads alive at once
    if (options.concurrentThreads)
    {
        workerThread1().CreateThread();
        workerThread2().CreateThread();

        FreeTests();
        MemberTests();
        MemberSpTests();
        FunctionTests();

        workerThread1().ExitThread();
        workerThread2().ExitThread();
    }
    else
    {
        std::cout << "Dispatch tests skipped (Options::concurrentThreads is false)" << std::endl;
    }

    // One thread at a time
    ThreadFullPolicyTests();

    StartGatePortableTest::Run(/*twoThreads=*/options.concurrentThreads);
}

} // namespace PortableTests
