/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief DelegateMQ on an ESP32 (dual-core Xtensa LX6) under ESP-IDF's FreeRTOS,
/// run under Espressif's QEMU.
///
/// What the other FreeRTOS samples can't show:
/// - SMP: ESP-IDF's FreeRTOS runs tasks on both cores at once, and
///   dmq::os::FreeRTOSCriticalSection takes its ESP_PLATFORM branch (a
///   portMUX_TYPE spinlock, which also excludes the other core).
/// - A hardware interrupt on Xtensa: an esp_timer callback dispatched from the
///   timer's interrupt (ESP_TIMER_ISR, 2 kHz) starts and
///   stops dmq::util::Timer objects while tasks on both cores do the same, and
///   xPortInIsrContext() must see every entry as an interrupt.
/// - dmq::util::IsrDelegate: the same interrupt calls a function on a worker
///   thread, through a drain task it wakes; every call once and in order.
///
/// Prints ALL TESTS PASSED when every check passed. A failed check faults
/// (DMQ_ASSERT_TRUE -> FaultHandler -> abort()), and the panic handler halts
/// (CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT), so the output ends there.

#include "DelegateMQ.h"
#include "PortableTests.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_chip_info.h"

#include <atomic>
#include <cstdio>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// The test task runs at TEST_TASK_PRIORITY; the portable tests' consumer
// threads run below it (FreeRTOS: higher number = higher priority), so posting
// never preempts the poster (see PortableTests::Options).
static constexpr UBaseType_t TEST_TASK_PRIORITY = 5;

static PortableTests::Options TestOptions()
{
    PortableTests::Options options;
    options.lowerPriority = [](Thread& t) { t.SetThreadPriority(1); };
    return options;
}

// Timer::ProcessTimers() every 10 ms, from the FreeRTOS timer service task
static void TimerCallback(TimerHandle_t) { Timer::ProcessTimers(); }

// --------------------------------------------------------------------------
// Basic delegates, synchronous and across threads
// --------------------------------------------------------------------------
static int Add(int a, int b) { return a + b; }

class Counter
{
public:
    void Increment(int n) { m_count += n; }
    int Get() const { return m_count; }
private:
    std::atomic<int> m_count{0};
};

static void DelegateTests()
{
    printf("\n[Test 1] Unicast and multicast delegates:\n");
    {
        auto add = MakeDelegate(&Add);
        DMQ_ASSERT_TRUE(add(2, 3) == 5);

        Counter counter;
        MulticastDelegate<void(int)> multicast;
        multicast += MakeDelegate(&counter, &Counter::Increment);
        multicast += MakeDelegate(&counter, &Counter::Increment);
        multicast(10);
        DMQ_ASSERT_TRUE(counter.Get() == 20);
        multicast -= MakeDelegate(&counter, &Counter::Increment);
        multicast(1);
        DMQ_ASSERT_TRUE(counter.Get() == 21);
        printf("  -> passed\n");
    }

    printf("\n[Test 2] Async and async-wait delegates on a worker thread:\n");
    {
        // A queue for the whole burst below. With the default (20) and FAULT,
        // the burst overflowed now and then: when the worker runs on the
        // other core, QEMU can run all 100 posts while that core waits for its
        // time slice. Full-queue behavior is Test 4's subject, not this one's.
        constexpr int CALLS = 100;
        Thread worker("Worker", CALLS + 1);
        DMQ_ASSERT_TRUE(worker.CreateThread());

        Counter counter;
        auto async = MakeDelegate(&counter, &Counter::Increment, worker);
        for (int i = 0; i < CALLS; i++)
            async(1);

        // Async-wait: runs on the worker and returns its result; the worker
        // runs the 100 messages above first (one FIFO queue)
        auto asyncWait = MakeDelegate(&Add, worker, WAIT_INFINITE);
        DMQ_ASSERT_TRUE(asyncWait(40, 2) == 42);
        DMQ_ASSERT_TRUE(counter.Get() == CALLS);

        worker.ExitThread();
        printf("  -> passed\n");
    }

    printf("\n[Test 3] Signal with a ScopedConnection:\n");
    {
        Counter counter;
        Signal<void(int)> signal;
        {
            ScopedConnection conn = signal.Connect(MakeDelegate(&counter, &Counter::Increment));
            signal(5);
        }
        signal(5);      // disconnected: no call
        DMQ_ASSERT_TRUE(counter.Get() == 5);
        printf("  -> passed\n");
    }
}

// --------------------------------------------------------------------------
// Timer API from a hardware interrupt (esp_timer, ISR dispatch, 2 kHz)
// --------------------------------------------------------------------------
static constexpr uint32_t ISR_HZ = 2000;
static constexpr uint32_t ISR_REARM_EVERY = 34;     // re-arm the one-shot every 17 ms
static Timer* s_isrOneShot = nullptr;               // started from the ISR, fires in the timer task
static Timer* s_isrChurn = nullptr;                 // started and stopped from the ISR
static volatile uint32_t s_isrCount = 0;
static volatile uint32_t s_isrNotInInterrupt = 0;   // entries xPortInIsrContext() didn't see as an interrupt
static std::atomic<int> s_isrOneShotFired{0};

static void OnIsrOneShot() { s_isrOneShotFired++; }

static void OnTimerIsr(void*)
{
    s_isrCount = s_isrCount + 1;
    if (!xPortInIsrContext())
        s_isrNotInInterrupt = s_isrNotInInterrupt + 1;
    if (s_isrCount % ISR_REARM_EVERY == 0)
        s_isrOneShot->Start(std::chrono::milliseconds(5), true);
    if (s_isrCount % 2)
        s_isrChurn->Start(std::chrono::milliseconds(1000));
    else
        s_isrChurn->Stop();
}

// Contends with the ISR and the test task from wherever the scheduler puts it
static std::atomic<bool> s_churnRun{false};
static std::atomic<unsigned> s_churnOps{0};
static void ChurnTask(void*)
{
    while (s_churnRun) {
        Timer local;
        local.Start(std::chrono::milliseconds(50));
        local.Stop();
        s_churnOps++;
        if (s_churnOps % 64 == 0)
            vTaskDelay(1);
    }
    vTaskDelete(nullptr);
}

static void IsrTimerTest()
{
    printf("\n[Test 6] Timer Start/Stop from a hardware timer interrupt, with a task on each core:\n");
    Timer oneShot;
    Timer churn;
    ScopedConnection conn = oneShot.OnExpired.Connect(MakeDelegate(&OnIsrOneShot));
    s_isrOneShot = &oneShot;
    s_isrChurn = &churn;

    // ESP_TIMER_ISR: the callback runs in the esp_timer interrupt itself, not
    // in its dispatch task (CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD)
    esp_timer_create_args_t args = {};
    args.callback = OnTimerIsr;
    args.dispatch_method = ESP_TIMER_ISR;
    args.name = "dmq-isr";
    esp_timer_handle_t isrTimer = nullptr;
    ESP_ERROR_CHECK(esp_timer_create(&args, &isrTimer));

    // A churn task pinned to each core, so the timer lock is contended across cores
    s_churnRun = true;
    xTaskCreatePinnedToCore(ChurnTask, "Churn0", 4096, nullptr, TEST_TASK_PRIORITY - 1, nullptr, 0);
    xTaskCreatePinnedToCore(ChurnTask, "Churn1", 4096, nullptr, TEST_TASK_PRIORITY - 1, nullptr, 1);

    ESP_ERROR_CHECK(esp_timer_start_periodic(isrTimer, 1000000 / ISR_HZ));

    const TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
    unsigned taskOps = 0;
    while (xTaskGetTickCount() < end) {
        Timer local;
        local.Start(std::chrono::milliseconds(50));
        churn.Stop();
        local.Stop();
        taskOps++;
        if (taskOps % 64 == 0)
            vTaskDelay(1);
    }

    ESP_ERROR_CHECK(esp_timer_stop(isrTimer));
    ESP_ERROR_CHECK(esp_timer_delete(isrTimer));
    s_churnRun = false;
    vTaskDelay(pdMS_TO_TICKS(100));     // churn tasks exit; a last one-shot fires
    oneShot.Stop();
    churn.Stop();

    const uint32_t isrCount = s_isrCount;
    const int fired = s_isrOneShotFired;
    printf("  -> %lu interrupts, %u test-task and %u churn-task loops, one-shot fired %d times\n",
           static_cast<unsigned long>(isrCount), taskOps, s_churnOps.load(), fired);
    // The checks don't assume a rate: QEMU delivers timer interrupts at the
    // host's pace
    DMQ_ASSERT_TRUE(isrCount >= 100);
    DMQ_ASSERT_TRUE(s_isrNotInInterrupt == 0);
    DMQ_ASSERT_TRUE(taskOps > 0 && s_churnOps > 0);
    // Each re-arm (5 ms one-shot) normally fires before the next one, but a
    // re-arm that lands before ProcessTimers() saw the expiry restarts it
    const int rearms = static_cast<int>(isrCount / ISR_REARM_EVERY);
    DMQ_ASSERT_TRUE(fired >= rearms / 2 && fired <= rearms + 1);
    printf("  -> passed\n");
}

// --------------------------------------------------------------------------
// IsrDelegate: the timer interrupt calls a function on a worker thread
// --------------------------------------------------------------------------
// The target is async, to a worker thread. A drain task runs
// IsrDelegates::ProcessAll(), woken by the post handler, which gives its
// semaphore from the interrupt.
static IsrDelegate<void(uint32_t, bool)>* s_isrPost = nullptr;
static volatile uint32_t s_isrPostSeq = 0;
static Thread* s_isrPostThread = nullptr;
static uint32_t s_isrPostLast = 0;          // written only on the worker thread
static volatile uint32_t s_isrPostReceived = 0;
static uint32_t s_isrPostGaps = 0;          // sequence numbers skipped (dropped posts)
static uint32_t s_isrPostBad = 0;           // out of order, not from an ISR, or wrong thread
static std::atomic<uint32_t> s_queueDropped{0};   // dropped by the worker's full queue
static SemaphoreHandle_t s_drainSem = nullptr;
static volatile bool s_drainRun = false;

static void PostFromIsr(void*)
{
    s_isrPostSeq = s_isrPostSeq + 1;
    s_isrPost->Post(s_isrPostSeq, xPortInIsrContext() != 0);
}

// IsrDelegates post handler: called by Post() in the interrupt
static void WakeDrainTask()
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_drainSem, &woken);
    portYIELD_FROM_ISR(woken);
}

static void DrainTask(void*)
{
    while (s_drainRun) {
        xSemaphoreTake(s_drainSem, pdMS_TO_TICKS(10));
        IsrDelegates::ProcessAll();
    }
    vTaskDelete(nullptr);
}

static void OnIsrPost(uint32_t seq, bool fromIsr)
{
    if (seq <= s_isrPostLast || !fromIsr || !s_isrPostThread->IsCurrentThread())
        s_isrPostBad++;
    else
        s_isrPostGaps += seq - s_isrPostLast - 1;
    s_isrPostLast = seq;
    s_isrPostReceived = s_isrPostReceived + 1;
}

static void IsrDelegateTest()
{
    printf("\n[Test 7] IsrDelegate posted from a hardware timer interrupt to a worker thread:\n");
    // FullPolicy::DROP: a burst forwarded faster than the worker runs it is
    // dropped and counted, rather than faulting (FAULT, the default) or
    // blocking the drain task (TIMEOUT)
    Thread worker("IsrPostWorker", 0, FullPolicy::DROP);
    worker.SetDroppedHandler(MakeDelegate(std::function<void(size_t)>([](size_t) { s_queueDropped++; })));
    DMQ_ASSERT_TRUE(worker.CreateThread());
    IsrDelegate<void(uint32_t, bool)> isrPost(MakeDelegate(&OnIsrPost, worker));
    s_isrPost = &isrPost;
    s_isrPostThread = &worker;

    s_drainSem = xSemaphoreCreateBinary();
    s_drainRun = true;
    xTaskCreate(DrainTask, "IsrDrain", 4096, nullptr, TEST_TASK_PRIORITY + 1, nullptr);
    IsrDelegates::SetPostHandler(&WakeDrainTask);

    esp_timer_create_args_t args = {};
    args.callback = PostFromIsr;
    args.dispatch_method = ESP_TIMER_ISR;
    args.name = "dmq-isr-post";
    esp_timer_handle_t isrTimer = nullptr;
    ESP_ERROR_CHECK(esp_timer_create(&args, &isrTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(isrTimer, 1000000 / ISR_HZ));
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_ERROR_CHECK(esp_timer_stop(isrTimer));
    vTaskDelay(pdMS_TO_TICKS(50));      // the last posts reach the worker

    // One more from task context: it also closes a gap left by final drops
    s_isrPostSeq = s_isrPostSeq + 1;
    DMQ_ASSERT_TRUE(isrPost.Post(s_isrPostSeq, true));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(esp_timer_delete(isrTimer));

    const uint32_t posted = s_isrPostSeq;
    const size_t ringDropped = isrPost.GetDroppedCount();
    const size_t dropped = ringDropped + s_queueDropped;
    printf("  -> %lu posted, %lu called on the worker, %lu dropped (%lu ring full, %lu worker queue full)\n",
           static_cast<unsigned long>(posted), static_cast<unsigned long>(s_isrPostReceived),
           static_cast<unsigned long>(dropped), static_cast<unsigned long>(ringDropped),
           static_cast<unsigned long>(s_queueDropped.load()));
    DMQ_ASSERT_TRUE(posted >= 100);
    DMQ_ASSERT_TRUE(s_isrPostBad == 0);                      // in order, from the ISR, on the worker
    DMQ_ASSERT_TRUE(s_isrPostReceived + dropped == posted);  // nothing lost or doubled
    DMQ_ASSERT_TRUE(s_isrPostGaps == dropped);

    IsrDelegates::SetPostHandler(nullptr);
    s_drainRun = false;
    vTaskDelay(pdMS_TO_TICKS(30));      // the drain task exits
    vSemaphoreDelete(s_drainSem);
    s_drainSem = nullptr;
    worker.ExitThread();
    printf("  -> passed\n");
}

// --------------------------------------------------------------------------
// Test 8: SetCoreAffinity()
// --------------------------------------------------------------------------
// A thread pinned to a core runs only there (xTaskCreatePinnedToCore()). Each
// one is asked many times which core runs it, with the other core busy too,
// so an unpinned task would be seen moving.
static int CoreOfCaller() { return static_cast<int>(xPortGetCoreID()); }

static void CoreAffinityTest()
{
    printf("\n[Test 8] SetCoreAffinity(): threads pinned to core 0 and core 1:\n");
    for (int core = 0; core < portNUM_PROCESSORS; core++) {
        Thread pinned("PinnedThread");
        pinned.SetCoreAffinity(1u << core);
        pinned.CreateThread();
        int wrongCore = 0;
        for (int i = 0; i < 200; i++) {
            if (MakeDelegate(&CoreOfCaller, pinned, WAIT_INFINITE)() != core)
                wrongCore++;
            if (i % 20 == 0)
                vTaskDelay(1);
        }
        pinned.ExitThread();
        printf("  -> core %d: 200 calls, %d on another core\n", core, wrongCore);
        DMQ_ASSERT_TRUE(wrongCore == 0);
    }

    // All cores (or 0) means no affinity
    Thread any("AnyCoreThread");
    any.SetCoreAffinity((1u << portNUM_PROCESSORS) - 1);
    any.CreateThread();
    DMQ_ASSERT_TRUE(MakeDelegate(&CoreOfCaller, any, WAIT_INFINITE)() >= 0);
    any.ExitThread();
}

// --------------------------------------------------------------------------
// Test 9: no false watchdog expiry when a heartbeat races the check
// --------------------------------------------------------------------------
// A thread under a watchdog runs on core 1, kept busy so it refreshes its
// heartbeat constantly; a task on core 0 runs Thread::WatchdogCheckAll() in a
// tight loop. A check that read the clock before the heartbeat could see a
// heartbeat one tick newer than its clock reading, and the unsigned 32-bit
// difference wrapped to ~49.7 days: a healthy thread reported as hung. On an
// ESP32-S3 the old code failed this within 50,000 checks. A false expiry
// faults (WatchdogHandler()), so this test passes by finishing.
static std::atomic<bool> s_raceStop{false};
static std::atomic<uint32_t> s_raceChecks{0};
static std::atomic<uint32_t> s_raceCalls{0};
static Thread* s_raceTarget = nullptr;

static void RaceWork() { s_raceCalls++; }

static void RaceHook(const WatchdogInfo& info)
{
    printf("  -> FALSE EXPIRY: %s reported stalled for %lld ms after %u checks\n", info.threadName,
           static_cast<long long>(info.stalledFor.count()), static_cast<unsigned>(s_raceChecks.load()));
}

static void RaceFeeder(void*)
{
    auto call = MakeDelegate(&RaceWork, *s_raceTarget);
    while (!s_raceStop) {
        for (int i = 0; i < 50; i++)
            call();
        vTaskDelay(1);
    }
    vTaskDelete(nullptr);
}

static void RaceChecker(void*)
{
    while (!s_raceStop) {
        for (int i = 0; i < 2000; i++) {
            Thread::WatchdogCheckAll();
            s_raceChecks++;
        }
        vTaskDelay(1);      // let the idle task run (ESP-IDF's task watchdog)
    }
    vTaskDelete(nullptr);
}

static void WatchdogRaceTest()
{
    printf("\n[Test 9] Watchdog check racing a busy thread's heartbeat (core 0 vs core 1):\n");
    SetWatchdogHook(&RaceHook);
    Thread target("RaceTarget", 0, FullPolicy::DROP);
    s_raceTarget = &target;
    target.SetCoreAffinity(1u << 1);
    target.CreateThread(std::chrono::seconds(2));

    xTaskCreatePinnedToCore(RaceFeeder, "RaceFeeder", 4096, nullptr, TEST_TASK_PRIORITY, nullptr, 1);
    xTaskCreatePinnedToCore(RaceChecker, "RaceChecker", 4096, nullptr, TEST_TASK_PRIORITY, nullptr, 0);
    vTaskDelay(pdMS_TO_TICKS(3000));
    s_raceStop = true;
    vTaskDelay(pdMS_TO_TICKS(50));      // both tasks see the flag and delete themselves

    target.ExitThread();
    SetWatchdogHook(nullptr);
    printf("  -> %u checks, %u calls, no false expiry\n",
           static_cast<unsigned>(s_raceChecks.load()), static_cast<unsigned>(s_raceCalls.load()));
    DMQ_ASSERT_TRUE(s_raceChecks > 0 && s_raceCalls > 0);
}

// --------------------------------------------------------------------------
// Test task
// --------------------------------------------------------------------------
static void RunTests()
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("\n=========================================\n");
    printf("   ESP32 (%d cores) DELEGATE TESTS       \n", chip.cores);
    printf("=========================================\n");

    DelegateTests();

    printf("\n[Test 4] Cross-thread dispatch & FullPolicy tests:\n");
    PortableTests::DelegateThreadsTests(TestOptions());

    printf("\n[Test 5] PacedDispatch & TimerDelegate tests:\n");
    PortableTests::TimerDelegateTests(TestOptions());

    IsrTimerTest();
    IsrDelegateTest();
    CoreAffinityTest();
    WatchdogRaceTest();

    printf("\n=========================================\n");
    printf("           ALL TESTS PASSED              \n");
    printf("=========================================\n");
}

static void TestTask(void*)
{
    RunTests();     // in a function, so its objects are destroyed before the task idles
    for (;;)
        vTaskDelay(portMAX_DELAY);
}

extern "C" void app_main(void)
{
    TimerHandle_t systemTimer = xTimerCreate("SysTimer", pdMS_TO_TICKS(10), pdTRUE, nullptr, TimerCallback);
    xTimerStart(systemTimer, 0);

    // Not pinned: ESP-IDF's scheduler runs it on either core
    xTaskCreate(TestTask, "TestTask", 16384, nullptr, TEST_TASK_PRIORITY, nullptr);
}
