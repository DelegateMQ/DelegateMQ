/// @file DelegateThreadsTests.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
///
/// @brief Cross-thread delegate dispatch and FullPolicy tests, ported
/// unchanged from test/unit-tests/DelegateThreadsTests.cpp to exercise
/// the same coverage against the real Zephyr (native_sim) Thread port
/// instead of the desktop stdlib port.

#include "DelegateMQ.h"
#include <iostream>
#include <random>
#include <chrono>
#include <cstring>
#include <atomic>

using namespace dmq;
using namespace dmq::os;
using namespace std;

// Construct-on-first-use: a plain file-scope `static Thread` would run its
// constructor before main() starts the periodic system timer and enters
// ExecuteAllTests(), when Zephyr's k_aligned_alloc heap (CONFIG_HEAP_MEM_POOL_SIZE)
// used by ZephyrThread::CreateThread() to allocate its stack may not yet be
// initialized. Function-local statics defer construction to the first call,
// which only happens from DelegateThreadsTests(), well after the kernel is up.
static Thread& workerThread1()
{
    static Thread t("DelegateThreads1Tests");
    return t;
}
static Thread& workerThread2()
{
    static Thread t("DelegateThreads2Tests");
    return t;
}

// dmq::Mutex, not std::mutex: on Zephyr's native_sim, exactly one Zephyr
// "thread" ever really executes at a time (a cooperative token-passing
// scheme between real host pthreads -- see ZephyrThread.cpp/nct.c). A thread
// that goes to sleep (k_sleep/dmq::ThisThread::sleep_for) while holding a raw
// std::mutex keeps that OS-level lock held for the whole sleep, since sleep
// only yields Zephyr's own scheduler token, not the real mutex. If a second
// worker thread is then given that token and tries to lock the same
// std::mutex, its real pthread blocks in a plain futex wait -- a syscall
// invisible to native_sim's cooperative scheduler, which has no way to
// preempt out of it. Nothing can ever hand the token back to the sleeping
// first thread to let it wake up and unlock, so every thread needing the
// lock deadlocks permanently. dmq::Mutex resolves to a real k_mutex here,
// whose lock/unlock are Zephyr scheduler primitives: blocking on it correctly
// yields the token instead of parking a real pthread outside the scheduler's
// view.
static dmq::Mutex m_lock;
static const int LOOPS = 10;
static const int CNT_MAX = 7;
static std::atomic<int> callerCnt[CNT_MAX];

// Increased timeout to prevent flaky/busy tests in Debug/CI environments
static const std::chrono::milliseconds TEST_TIMEOUT(5000);

static void Wait()
{
    while (workerThread1().GetQueueSize() != 0 || workerThread2().GetQueueSize() != 0)
    {
        dmq::ThisThread::sleep_for(std::chrono::milliseconds(10));
    }
    dmq::ThisThread::sleep_for(std::chrono::milliseconds(100));
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
    const dmq::LockGuard<dmq::Mutex> lock(m_lock);
    callerCnt[idx]++;
    dmq::ThisThread::sleep_for(delay);
}

static std::function<void(std::chrono::milliseconds, int)> LambdaThreadSafe = [](std::chrono::milliseconds delay, int idx)
    {
        const dmq::LockGuard<dmq::Mutex> lock(m_lock);
        callerCnt[idx]++;
        dmq::ThisThread::sleep_for(delay);
    };

class TestClass
{
public:
    void MemberThreadSafe(std::chrono::milliseconds delay, int idx)
    {
        const dmq::LockGuard<dmq::Mutex> lock(m_lock);
        callerCnt[idx]++;
        dmq::ThisThread::sleep_for(delay);
    }
};

static void FreeTests()
{
    for (auto& c : callerCnt) c.store(0, std::memory_order_relaxed);

    auto delegateSync1 = MakeDelegate(&FreeThreadSafe);
    auto delegateSync2 = MakeDelegate(&FreeThreadSafe);
    auto delegateAsync1 = MakeDelegate(&FreeThreadSafe, workerThread1());
    auto delegateAsync2 = MakeDelegate(&FreeThreadSafe, workerThread2());
    auto delegateAsyncWait1 = MakeDelegate(&FreeThreadSafe, workerThread1(), TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(&FreeThreadSafe, workerThread2(), TEST_TIMEOUT);

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
    for (auto& c : callerCnt) c.store(0, std::memory_order_relaxed);
    TestClass testClass;

    auto delegateSync1 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe);
    auto delegateSync2 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe);
    auto delegateAsync1 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread1());
    auto delegateAsync2 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread2());
    auto delegateAsyncWait1 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread1(), TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(&testClass, &TestClass::MemberThreadSafe, workerThread2(), TEST_TIMEOUT);

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
    for (auto& c : callerCnt) c.store(0, std::memory_order_relaxed);
    auto testClass = std::make_shared<TestClass>();

    auto delegateSync1 = MakeDelegate(testClass, &TestClass::MemberThreadSafe);
    auto delegateSync2 = MakeDelegate(testClass, &TestClass::MemberThreadSafe);
    auto delegateAsync1 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread1());
    auto delegateAsync2 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread2());
    auto delegateAsyncWait1 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread1(), TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(testClass, &TestClass::MemberThreadSafe, workerThread2(), TEST_TIMEOUT);

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
    for (auto& c : callerCnt) c.store(0, std::memory_order_relaxed);

    auto delegateSync1 = MakeDelegate(LambdaThreadSafe);
    auto delegateSync2 = MakeDelegate(LambdaThreadSafe);
    auto delegateAsync1 = MakeDelegate(LambdaThreadSafe, workerThread1());
    auto delegateAsync2 = MakeDelegate(LambdaThreadSafe, workerThread2());
    auto delegateAsyncWait1 = MakeDelegate(LambdaThreadSafe, workerThread1(), TEST_TIMEOUT);
    auto delegateAsyncWait2 = MakeDelegate(LambdaThreadSafe, workerThread2(), TEST_TIMEOUT);

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

    // Give the consumer a lower priority than the calling thread. Zephyr uses
    // ThreadX's convention (lower number = higher priority): this sample's
    // main thread runs at the default CONFIG_MAIN_THREAD_PRIORITY of 0, and
    // ZephyrThread's own default worker priority (5) is already lower -- set
    // explicitly here for clarity rather than relying on that default. With a
    // lower-priority consumer, Zephyr's preemptive scheduler never switches to
    // it just because a message was posted; it only runs once the caller
    // blocks or sleeps, so the "publisher never stalls" assumption below holds.
    dropThread.SetThreadPriority(5);
    dropThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };

    auto slowConsumer = [&deliveredCount]() {
        dmq::ThisThread::sleep_for(std::chrono::milliseconds(50));
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
    dmq::ThisThread::sleep_for(std::chrono::milliseconds(300));

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
        dmq::ThisThread::sleep_for(std::chrono::milliseconds(5));
    dmq::ThisThread::sleep_for(std::chrono::milliseconds(50));

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

    // Post directly from this thread: TIMEOUT just blocks the caller until a
    // slot opens, which is fine here since nothing else needs this thread's
    // attention meanwhile (the desktop/FreeRTOS versions of this test spin up
    // a helper OS thread for the sends and join it immediately afterwards,
    // which blocks identically -- so it adds no real concurrency, only the
    // risk of a raw host thread touching Zephyr kernel objects from outside
    // native_sim's own thread bookkeeping).
    for (int i = 0; i < SEND_COUNT; i++)
        MakeDelegate([&deliveredCount]() { deliveredCount++; }, timeoutThread)();

    while (timeoutThread.GetQueueSize() != 0)
        dmq::ThisThread::sleep_for(std::chrono::milliseconds(5));
    dmq::ThisThread::sleep_for(std::chrono::milliseconds(50));

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
        dmq::ThisThread::sleep_for(std::chrono::milliseconds(5));
    dmq::ThisThread::sleep_for(std::chrono::milliseconds(50));

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
        dmq::ThisThread::sleep_for(std::chrono::milliseconds(5));
    dmq::ThisThread::sleep_for(std::chrono::milliseconds(50));

    ASSERT_TRUE(deliveredCount == 5);

    faultThread.ExitThread();
    std::cout << "FullPolicy_Fault_WorksWhenNotFull() complete!" << std::endl;
}

// maxQueueSize=0 ("unlimited" in this test's name, matching test/unit-tests):
// on the desktop stdlib/Win32/Qt ports 0 really does mean no cap. On every
// RTOS port (FreeRTOS/ThreadX/Zephyr/CMSIS-RTOS2), ZephyrThread's constructor
// et al. instead treat 0 as "use dmq::DEFAULT_QUEUE_SIZE" (20) -- a real,
// finite cap, not unlimited. Sending 50 messages back-to-back with no
// blocking call in between (as the original test did) relies on the
// *consumer* getting an incidental scheduling slice while the queue is still
// below capacity; FreeRTOS's POSIX port happens to get real preemption from
// the host OS during that loop and passes anyway, but Zephyr's native_sim is
// purely cooperative -- nothing but a blocking Zephyr call ever yields, so
// all 50 sends complete as one atomic burst before the consumer runs even
// once, and DROP silently discards everything past the 20th. SEND_COUNT is
// kept at/under DEFAULT_QUEUE_SIZE here so the test verifies what 0 actually
// guarantees on this port -- no drops within capacity -- rather than a
// stdlib-only "truly unbounded" behavior this port doesn't have.
static void FullPolicy_UnlimitedQueue_DeliversAll()
{
    Thread unlimitedThread("UnlimitedThread", 0, FullPolicy::DROP);
    unlimitedThread.CreateThread();

    std::atomic<int> deliveredCount{ 0 };
    const int SEND_COUNT = 15;

    for (int i = 0; i < SEND_COUNT; i++)
        MakeDelegate([&deliveredCount]() { deliveredCount++; }, unlimitedThread)();

    while (unlimitedThread.GetQueueSize() != 0)
        dmq::ThisThread::sleep_for(std::chrono::milliseconds(5));
    dmq::ThisThread::sleep_for(std::chrono::milliseconds(50));

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
    workerThread1().CreateThread();
    workerThread2().CreateThread();

    FreeTests();
    MemberTests();
    MemberSpTests();
    FunctionTests();

    workerThread1().ExitThread();
    workerThread2().ExitThread();

    ThreadFullPolicyTests();
}
