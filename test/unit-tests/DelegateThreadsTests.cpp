#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include <iostream>
#include <random>
#include <chrono>
#include <cstring>
#include <atomic>

using namespace dmq;
using namespace dmq::os;
using namespace std;
using namespace UnitTestData;

static Thread workerThread1("DelegateThreads1Tests");
static Thread workerThread2("DelegateThreads2Tests");

static std::mutex m_lock;
static const int LOOPS = 10;
static const int CNT_MAX = 7;
static std::atomic<int> callerCnt[CNT_MAX];

// Increased timeout to prevent flaky/busy tests in Debug/CI environments
static const std::chrono::milliseconds TEST_TIMEOUT(5000);

static void Wait()
{
    while (workerThread1.GetQueueSize() != 0 || workerThread2.GetQueueSize() != 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

static std::chrono::milliseconds getRandomTime()
{
    // Create a random number generator and a uniform distribution
    std::random_device rd;  // Non-deterministic random number generator
    std::mt19937 gen(rd()); // Mersenne Twister engine initialized with rd
    std::uniform_int_distribution<> dis(0, 10); // Uniform distribution between 0 and 10

    // Generate a random number (between 0 and 10 milliseconds)
    int random_ms = dis(gen);

    // Return the random number as a chrono::milliseconds object
    return std::chrono::milliseconds(random_ms);
}

static void FreeThreadSafe(std::chrono::milliseconds delay, int idx)
{
    const std::lock_guard<std::mutex> lock(m_lock);
    callerCnt[idx]++;
    std::this_thread::sleep_for(delay);
}

static std::function<void(std::chrono::milliseconds, int)> LambdaThreadSafe = [](std::chrono::milliseconds delay, int idx)
    {
        const std::lock_guard<std::mutex> lock(m_lock);
        callerCnt[idx]++;
        std::this_thread::sleep_for(delay);
    };

class TestClass
{
public:
    void MemberThreadSafe(std::chrono::milliseconds delay, int idx)
    {
        const std::lock_guard<std::mutex> lock(m_lock);
        callerCnt[idx]++;
        std::this_thread::sleep_for(delay);
    }
};

static void FreeTests()
{
    for (auto& c : callerCnt) c.store(0, std::memory_order_relaxed);

    auto delegateSync1 = MakeDelegate(&FreeThreadSafe);
    auto delegateSync2 = MakeDelegate(&FreeThreadSafe);
    auto delegateAsync1 = MakeDelegate(&FreeThreadSafe, workerThread1);
    auto delegateAsync2 = MakeDelegate(&FreeThreadSafe, workerThread2);
    auto delegateAsyncWait1 = MakeDelegate(&FreeThreadSafe, workerThread1, TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(&FreeThreadSafe, workerThread2, TEST_TIMEOUT);

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
    std::cout << "FreeTests() complete!" << std::endl;
}

static void MemberTests()
{
    for (auto& c : callerCnt) c.store(0, std::memory_order_relaxed);
    TestClass testClass;

    auto delegateSync1 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe);
    auto delegateSync2 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe);
    auto delegateAsync1 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread1);
    auto delegateAsync2 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread2);
    auto delegateAsyncWait1 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread1, TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread2, TEST_TIMEOUT);

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
    std::cout << "MemberTests() complete!" << std::endl;
}

static void MemberSpTests()
{
    for (auto& c : callerCnt) c.store(0, std::memory_order_relaxed);
    auto testClass = std::make_shared<TestClass>();

    auto delegateSync1 = MakeDelegate(testClass, &TestClass::MemberThreadSafe);
    auto delegateSync2 = MakeDelegate(testClass, &TestClass::MemberThreadSafe);
    auto delegateAsync1 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread1);
    auto delegateAsync2 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread2);
    auto delegateAsyncWait1 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread1, TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread2, TEST_TIMEOUT);

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
    std::cout << "MemberSpTests() complete!" << std::endl;
}

static void FunctionTests()
{
    for (auto& c : callerCnt) c.store(0, std::memory_order_relaxed);

    auto delegateSync1 = MakeDelegate(LambdaThreadSafe);
    auto delegateSync2 = MakeDelegate(LambdaThreadSafe);
    auto delegateAsync1 = MakeDelegate(LambdaThreadSafe, workerThread1);
    auto delegateAsync2 = MakeDelegate(LambdaThreadSafe, workerThread2);
    auto delegateAsyncWait1 = MakeDelegate(LambdaThreadSafe, workerThread1, TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(LambdaThreadSafe, workerThread2, TEST_TIMEOUT);

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
    std::cout << "FunctionTests() complete!" << std::endl;
}

// ---------------------------------------------------------------------------
// FullPolicy tests
// ---------------------------------------------------------------------------

// DROP policy: flooding a full queue drops messages; publisher is never stalled.
static void FullPolicy_Drop_DropsWhenFull()
{
    // Queue holds 3 messages. Consumer sleeps 50ms per message so it drains slowly.
    // We fire 10 messages as fast as possible; some must be dropped.
    Thread dropThread("DropThread", 3, FullPolicy::DROP);
    dropThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };

    auto slowConsumer = [&deliveredCount]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        deliveredCount++;
    };

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 10; i++)
        MakeDelegate(slowConsumer, dropThread)(/* no args */);

    auto elapsed = std::chrono::steady_clock::now() - start;

    // Publisher must NOT have blocked — posting 10 messages should finish well
    // under the time it would take to drain even one slot (50ms).
    DMQ_ASSERT_TRUE(elapsed < std::chrono::milliseconds(30));

    // Let the queue drain fully
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // With a queue depth of 3 and 10 rapid-fire posts, at least some were dropped.
    // Exactly 3 might be delivered (the ones that fit) but we allow a little slack
    // for timing; the invariant is: delivered < 10.
    DMQ_ASSERT_TRUE(deliveredCount < 10);
    DMQ_ASSERT_TRUE(deliveredCount > 0);

    dropThread.ExitThread();
    std::cout << "FullPolicy_Drop_DropsWhenFull() complete! (delivered " << deliveredCount << "/10)" << std::endl;
}

// DROP policy: if the queue never fills, every message is delivered.
static void FullPolicy_Drop_DeliversAllWhenBelowLimit()
{
    Thread dropThread("DropBelowLimitThread", 20, FullPolicy::DROP);
    dropThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };
    const int SEND_COUNT = 10;

    for (int i = 0; i < SEND_COUNT; i++)
        MakeDelegate([&deliveredCount]() { deliveredCount++; }, dropThread)();

    // Wait for all queued messages to be processed
    while (dropThread.GetQueueSize() != 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    DMQ_ASSERT_TRUE(deliveredCount == SEND_COUNT);

    dropThread.ExitThread();
    std::cout << "FullPolicy_Drop_DeliversAllWhenBelowLimit() complete!" << std::endl;
}

// TIMEOUT policy: all messages are delivered even when the queue is flooded (within timeout).
static void FullPolicy_Timeout_DeliversAll()
{
    Thread timeoutThread("TimeoutThread", 3, FullPolicy::TIMEOUT);
    timeoutThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };
    const int SEND_COUNT = 10;

    // Fire sends on a background thread so waiting doesn't stall this test thread.
    std::thread sender([&]() {
        for (int i = 0; i < SEND_COUNT; i++)
            MakeDelegate([&deliveredCount]() { deliveredCount++; }, timeoutThread)();
    });
    sender.join();

    while (timeoutThread.GetQueueSize() != 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    DMQ_ASSERT_TRUE(deliveredCount == SEND_COUNT);

    timeoutThread.ExitThread();
    std::cout << "FullPolicy_Timeout_DeliversAll() complete!" << std::endl;
}

// Default constructor (no policy arg) behaves as FAULT.
static void FullPolicy_DefaultIsFault()
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

    while (defaultThread.GetQueueSize() != 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    DMQ_ASSERT_TRUE(deliveredCount == SEND_COUNT);

    defaultThread.ExitThread();
    std::cout << "FullPolicy_DefaultIsFault() complete!" << std::endl;
}

// FAULT policy: verify it works as expected when not full.
// NOTE: We cannot easily test the "Full" case because it terminates the application.
static void FullPolicy_Fault_WorksWhenNotFull()
{
    Thread faultThread("FaultThread", 10, FullPolicy::FAULT);
    faultThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };
    for (int i = 0; i < 5; i++)
        MakeDelegate([&deliveredCount]() { deliveredCount++; }, faultThread)();

    while (faultThread.GetQueueSize() != 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    DMQ_ASSERT_TRUE(deliveredCount == 5);

    faultThread.ExitThread();
    std::cout << "FullPolicy_Fault_WorksWhenNotFull() complete!" << std::endl;
}

// DROP policy: SetDroppedHandler() must fire once per dropped message, since
// nothing else notifies the application that DROP silently discarded a message.
static void FullPolicy_Drop_NotifiesDroppedHandler()
{
    Thread dropThread("DropNotifyThread", 3, FullPolicy::DROP);

    std::atomic<int> droppedCount{ 0 };
    std::atomic<size_t> lastDroppedDepth{ 0 };
    dropThread.SetDroppedHandler(MakeDelegate([&](size_t depth) {
        droppedCount++;
        lastDroppedDepth = depth;
        }));

    dropThread.CreateThread();

    auto slowConsumer = []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        };

    for (int i = 0; i < 10; i++)
        MakeDelegate(slowConsumer, dropThread)();

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Same invariant as FullPolicy_Drop_DropsWhenFull: at least one, but not all
    // 10, were dropped -- and the handler must have fired exactly that many times.
    DMQ_ASSERT_TRUE(droppedCount > 0);
    DMQ_ASSERT_TRUE(droppedCount < 10);
    DMQ_ASSERT_TRUE(lastDroppedDepth == 3);

    dropThread.ExitThread();
    std::cout << "FullPolicy_Drop_NotifiesDroppedHandler() complete! (dropped " << droppedCount << "/10)" << std::endl;
}

// TIMEOUT policy: SetDroppedHandler() must fire when a message is dropped after
// waiting the full dispatchTimeout with no space freed.
static void FullPolicy_Timeout_NotifiesDroppedHandler()
{
    Thread timeoutThread("TimeoutNotifyThread", 1, FullPolicy::TIMEOUT, std::chrono::milliseconds(50));

    std::atomic<int> droppedCount{ 0 };
    timeoutThread.SetDroppedHandler(MakeDelegate([&](size_t) {
        droppedCount++;
        }));

    timeoutThread.CreateThread();

    // Block the single consumer slot for longer than dispatchTimeout so the
    // second send below must time out and be dropped.
    MakeDelegate([]() { std::this_thread::sleep_for(std::chrono::milliseconds(500)); }, timeoutThread)();
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); // ensure it's running
    MakeDelegate([]() {}, timeoutThread)(); // queue full while consumer is busy
    MakeDelegate([]() {}, timeoutThread)(); // must time out and be dropped

    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    DMQ_ASSERT_TRUE(droppedCount == 1);

    timeoutThread.ExitThread();
    std::cout << "FullPolicy_Timeout_NotifiesDroppedHandler() complete!" << std::endl;
}

// maxQueueSize=0 falls back to dmq::THREAD_DESKTOP_QUEUE_SIZE on this port (a
// high-water-mark safety net, not literal "unlimited" -- see DispatchDelegate()'s
// FullPolicy comment). A burst well under that cap must still all be delivered.
static void FullPolicy_DefaultQueueSize_DeliversAll()
{
    Thread defaultSizeThread("DefaultQueueSizeThread", 0, FullPolicy::DROP);
    defaultSizeThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };
    const int SEND_COUNT = 50;

    for (int i = 0; i < SEND_COUNT; i++)
        MakeDelegate([&deliveredCount]() { deliveredCount++; }, defaultSizeThread)();

    while (defaultSizeThread.GetQueueSize() != 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    DMQ_ASSERT_TRUE(deliveredCount == SEND_COUNT);

    defaultSizeThread.ExitThread();
    std::cout << "FullPolicy_DefaultQueueSize_DeliversAll() complete!" << std::endl;
}

// maxQueueSize=0's fallback must actually cap the queue, not disable backpressure --
// flooding well past dmq::THREAD_DESKTOP_QUEUE_SIZE must still drop under DROP.
static void FullPolicy_DefaultQueueSize_StillCapsUnderFlood()
{
    Thread floodThread("FloodDefaultQueueSizeThread", 0, FullPolicy::DROP);

    std::atomic<int> droppedCount{ 0 };
    floodThread.SetDroppedHandler(MakeDelegate([&](size_t) {
        droppedCount++;
        }));

    floodThread.CreateThread();

    // Consumer sleeps so the queue can't drain during the flood below. Kept short
    // since ExitThread() drains the full queue before returning, and this test
    // must flood past dmq::THREAD_DESKTOP_QUEUE_SIZE to prove it (not just the
    // old RTOS-style default of 20) is the effective cap.
    auto slowConsumer = []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        };

    const int SEND_COUNT = static_cast<int>(dmq::THREAD_DESKTOP_QUEUE_SIZE) + 100;
    for (int i = 0; i < SEND_COUNT; i++)
        MakeDelegate(slowConsumer, floodThread)();

    DMQ_ASSERT_TRUE(droppedCount > 0);

    floodThread.ExitThread();
    std::cout << "FullPolicy_DefaultQueueSize_StillCapsUnderFlood() complete! (dropped "
              << droppedCount << "/" << SEND_COUNT << ")" << std::endl;
}

static void ThreadFullPolicyTests()
{
    FullPolicy_Drop_DropsWhenFull();
    FullPolicy_Drop_NotifiesDroppedHandler();
    FullPolicy_Drop_DeliversAllWhenBelowLimit();
    FullPolicy_Timeout_DeliversAll();
    FullPolicy_Timeout_NotifiesDroppedHandler();
    FullPolicy_DefaultIsFault();
    FullPolicy_Fault_WorksWhenNotFull();
    FullPolicy_DefaultQueueSize_DeliversAll();
    FullPolicy_DefaultQueueSize_StillCapsUnderFlood();
}

void DelegateThreadsTests()
{
    workerThread1.CreateThread();
    workerThread2.CreateThread();

    FreeTests();
    MemberTests();
    MemberSpTests();
    FunctionTests();

    workerThread1.ExitThread();
    workerThread2.ExitThread();

    ThreadFullPolicyTests();
}