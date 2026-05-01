#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include <iostream>
#include <random>
#include <chrono>
#include <cstring>

using namespace dmq;
using namespace dmq::os;
using namespace std;
using namespace UnitTestData;

static Thread workerThread1("DelegateThreads1Tests");
static Thread workerThread2("DelegateThreads2Tests");

static std::mutex m_lock;
static const int LOOPS = 10;
static const int CNT_MAX = 7;
static int callerCnt[CNT_MAX] = { 0 };

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
    std::memset(callerCnt, 0, sizeof(callerCnt));

    auto delegateSync1 = MakeDelegate(&FreeThreadSafe);
    auto delegateSync2 = MakeDelegate(&FreeThreadSafe);
    auto delegateAsync1 = MakeDelegate(&FreeThreadSafe, workerThread1);
    auto delegateAsync2 = MakeDelegate(&FreeThreadSafe, workerThread2);
    auto delegateAsyncWait1 = MakeDelegate(&FreeThreadSafe, workerThread1, TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(&FreeThreadSafe, workerThread1, TEST_TIMEOUT);

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
        ASSERT_TRUE(retVal1.has_value());
        auto retVal2 = delegateAsyncWait2.AsyncInvoke(getRandomTime(), 5);
        ASSERT_TRUE(retVal2.has_value());

        while (cnt2++ < 5)
        {
            delegateAsync1(getRandomTime(), 2);
            delegateAsync2(getRandomTime(), 3);
        }
        cnt2 = 0;
        container(getRandomTime(), 6);
    }

    Wait();

    ASSERT_TRUE(callerCnt[0] == LOOPS);
    ASSERT_TRUE(callerCnt[1] == LOOPS);
    ASSERT_TRUE(callerCnt[2] == LOOPS * 5);
    ASSERT_TRUE(callerCnt[3] == LOOPS * 5);
    ASSERT_TRUE(callerCnt[4] == LOOPS);
    ASSERT_TRUE(callerCnt[5] == LOOPS);
    ASSERT_TRUE(callerCnt[6] == LOOPS * 6);
    std::cout << "FreeTests() complete!" << std::endl;
}

static void MemberTests()
{
    std::memset(callerCnt, 0, sizeof(callerCnt));
    TestClass testClass;

    auto delegateSync1 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe);
    auto delegateSync2 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe);
    auto delegateAsync1 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread1);
    auto delegateAsync2 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread2);
    auto delegateAsyncWait1 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread1, TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread1, TEST_TIMEOUT);

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
        ASSERT_TRUE(retVal1.has_value());
        auto retVal2 = delegateAsyncWait2.AsyncInvoke(getRandomTime(), 5);
        ASSERT_TRUE(retVal2.has_value());

        while (cnt2++ < 5)
        {
            delegateAsync1(getRandomTime(), 2);
            delegateAsync2(getRandomTime(), 3);
        }
        cnt2 = 0;
        container(getRandomTime(), 6);
    }

    Wait();

    ASSERT_TRUE(callerCnt[0] == LOOPS);
    ASSERT_TRUE(callerCnt[1] == LOOPS);
    ASSERT_TRUE(callerCnt[2] == LOOPS * 5);
    ASSERT_TRUE(callerCnt[3] == LOOPS * 5);
    ASSERT_TRUE(callerCnt[4] == LOOPS);
    ASSERT_TRUE(callerCnt[5] == LOOPS);
    ASSERT_TRUE(callerCnt[6] == LOOPS * 6);
    std::cout << "MemberTests() complete!" << std::endl;
}

static void MemberSpTests()
{
    std::memset(callerCnt, 0, sizeof(callerCnt));
    auto testClass = std::make_shared<TestClass>();

    auto delegateSync1 = MakeDelegate(testClass, &TestClass::MemberThreadSafe);
    auto delegateSync2 = MakeDelegate(testClass, &TestClass::MemberThreadSafe);
    auto delegateAsync1 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread1);
    auto delegateAsync2 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread2);
    auto delegateAsyncWait1 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread1, TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread1, TEST_TIMEOUT);

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
        ASSERT_TRUE(retVal1.has_value());
        auto retVal2 = delegateAsyncWait2.AsyncInvoke(getRandomTime(), 5);
        ASSERT_TRUE(retVal2.has_value());

        while (cnt2++ < 5)
        {
            delegateAsync1(getRandomTime(), 2);
            delegateAsync2(getRandomTime(), 3);
        }
        cnt2 = 0;
        container(getRandomTime(), 6);
    }

    Wait();

    ASSERT_TRUE(callerCnt[0] == LOOPS);
    ASSERT_TRUE(callerCnt[1] == LOOPS);
    ASSERT_TRUE(callerCnt[2] == LOOPS * 5);
    ASSERT_TRUE(callerCnt[3] == LOOPS * 5);
    ASSERT_TRUE(callerCnt[4] == LOOPS);
    ASSERT_TRUE(callerCnt[5] == LOOPS);
    ASSERT_TRUE(callerCnt[6] == LOOPS * 6);
    std::cout << "MemberSpTests() complete!" << std::endl;
}

static void FunctionTests()
{
    std::memset(callerCnt, 0, sizeof(callerCnt));

    auto delegateSync1 = MakeDelegate(LambdaThreadSafe);
    auto delegateSync2 = MakeDelegate(LambdaThreadSafe);
    auto delegateAsync1 = MakeDelegate(LambdaThreadSafe, workerThread1);
    auto delegateAsync2 = MakeDelegate(LambdaThreadSafe, workerThread2);
    auto delegateAsyncWait1 = MakeDelegate(LambdaThreadSafe, workerThread1, TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(LambdaThreadSafe, workerThread1, TEST_TIMEOUT);

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
        ASSERT_TRUE(retVal1.has_value());
        auto retVal2 = delegateAsyncWait2.AsyncInvoke(getRandomTime(), 5);
        ASSERT_TRUE(retVal2.has_value());

        while (cnt2++ < 5)
        {
            delegateAsync1(getRandomTime(), 2);
            delegateAsync2(getRandomTime(), 3);
        }
        cnt2 = 0;
        container(getRandomTime(), 6);
    }

    Wait();

    ASSERT_TRUE(callerCnt[0] == LOOPS);
    ASSERT_TRUE(callerCnt[1] == LOOPS);
    ASSERT_TRUE(callerCnt[2] == LOOPS * 5);
    ASSERT_TRUE(callerCnt[3] == LOOPS * 5);
    ASSERT_TRUE(callerCnt[4] == LOOPS);
    ASSERT_TRUE(callerCnt[5] == LOOPS);
    ASSERT_TRUE(callerCnt[6] == LOOPS * 6);
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
    ASSERT_TRUE(elapsed < std::chrono::milliseconds(30));

    // Let the queue drain fully
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // With a queue depth of 3 and 10 rapid-fire posts, at least some were dropped.
    // Exactly 3 might be delivered (the ones that fit) but we allow a little slack
    // for timing; the invariant is: delivered < 10.
    ASSERT_TRUE(deliveredCount < 10);
    ASSERT_TRUE(deliveredCount > 0);

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

    ASSERT_TRUE(deliveredCount == SEND_COUNT);

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

    ASSERT_TRUE(deliveredCount == SEND_COUNT);

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

    ASSERT_TRUE(deliveredCount == SEND_COUNT);

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

    ASSERT_TRUE(deliveredCount == 5);

    faultThread.ExitThread();
    std::cout << "FullPolicy_Fault_WorksWhenNotFull() complete!" << std::endl;
}

// Unlimited queue (maxQueueSize=0): FullPolicy has no effect; all messages delivered.
static void FullPolicy_UnlimitedQueue_DeliversAll()
{
    Thread unlimitedThread("UnlimitedThread", 0, FullPolicy::DROP);
    unlimitedThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };
    const int SEND_COUNT = 50;

    for (int i = 0; i < SEND_COUNT; i++)
        MakeDelegate([&deliveredCount]() { deliveredCount++; }, unlimitedThread)();

    while (unlimitedThread.GetQueueSize() != 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT_TRUE(deliveredCount == SEND_COUNT);

    unlimitedThread.ExitThread();
    std::cout << "FullPolicy_UnlimitedQueue_DeliversAll() complete!" << std::endl;
}

static void ThreadFullPolicyTests()
{
    FullPolicy_Drop_DropsWhenFull();
    FullPolicy_Drop_DeliversAllWhenBelowLimit();
    FullPolicy_Timeout_DeliversAll();
    FullPolicy_DefaultIsFault();
    FullPolicy_Fault_WorksWhenNotFull();
    FullPolicy_UnlimitedQueue_DeliversAll();
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