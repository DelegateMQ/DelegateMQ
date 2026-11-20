/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.

#include "DelegateMQ.h" 
#include "../../../../test/portable/StartGatePortableTest.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "bsp.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

// --------------------------------------------------------------------------
// FREERTOS CONFIGURATION & HELPERS
// --------------------------------------------------------------------------
#define mainTIMER_FREQUENCY_MS pdMS_TO_TICKS(10UL)
static TimerHandle_t xSystemTimer = nullptr;
static void PrintAllTestsPassed();

// --------------------------------------------------------------------------
// START GATE TEST HELPERS (Test 9)
// --------------------------------------------------------------------------
static std::atomic<int> s_gateRan{0};
static void GateCount(int) { s_gateRan++; }

// Created in main_delegate() BEFORE vTaskStartScheduler(): FreeRTOSThread
// can't wait for the task then, so its start handler runs when the scheduler
// starts. With a gate it then holds until Test 9 opens it. Constructed there,
// not as globals: their constructors create RTOS objects, which needs the
// FreeRTOS heap that main() sets up.
static StartGate* s_earlyGate = nullptr;
static Thread* s_earlyThread = nullptr;
static std::atomic<bool> s_earlyStarted{false};

static void TimerCallback(TimerHandle_t xTimerHandle) {
    // A task (the FreeRTOS timer daemon), never an ISR: ProcessTimers()
    // takes OS mutexes on an RTOS port
    Timer::ProcessTimers();
}

// --------------------------------------------------------------------------
// EVENT-DRIVEN TIMER DRIVER (Test 13)
// --------------------------------------------------------------------------
// In place of the 10 ms software timer: a task that runs ProcessTimers() only
// when a timer is due. It waits for Timer::GetNextExpiry(), or for a
// notification from the schedule-changed handler when Start() adds a timer
// that may be due sooner. A Start() between GetNextExpiry() and the wait isn't
// missed: the notification stays pending, so the wait returns at once. With
// configUSE_TICKLESS_IDLE, the kernel can then sleep the CPU between deadlines.
static TaskHandle_t s_driverTask = nullptr;
static volatile bool s_driverRun = false;
static volatile uint32_t s_driverWakes = 0;

// Timer::SetScheduleChangedHandler(): called by Start(). Test 13 starts timers
// from tasks only; an app that starts them from an ISR would also need
// vTaskNotifyGiveFromISR() there.
static void WakeTimerDriver() { xTaskNotifyGive(s_driverTask); }

// A GetNextExpiry() result as a FreeRTOS wait, rounded up so the task never
// wakes before the deadline; capped at an hour (pdMS_TO_TICKS() overflows)
static TickType_t ToWaitTicks(dmq::Duration d) {
    if (d == dmq::Duration::max())
        return portMAX_DELAY;
    const uint64_t ms = std::min<uint64_t>(static_cast<uint64_t>(d.count()), 3600000u);
    return static_cast<TickType_t>((ms * configTICK_RATE_HZ + 999) / 1000);
}

static void TimerDriverTask(void*) {
    while (s_driverRun) {
        Timer::ProcessTimers();
        ulTaskNotifyTake(pdTRUE, ToWaitTicks(Timer::GetNextExpiry()));
        s_driverWakes = s_driverWakes + 1;
    }
    vTaskDelete(nullptr);
}

// Test 13's timer callbacks: run in the driver task, record the tick they ran at
static volatile uint32_t s_firedA = 0;
static volatile uint32_t s_firedB = 0;
static volatile TickType_t s_tickA = 0;
static volatile TickType_t s_tickB = 0;
static void OnDriverTimerA() { s_tickA = xTaskGetTickCount(); s_firedA = s_firedA + 1; }
static void OnDriverTimerB() { s_tickB = xTaskGetTickCount(); s_firedB = s_firedB + 1; }

#if defined(BSP_TARGET)
// --------------------------------------------------------------------------
// HARDWARE ISR TEST HELPERS (Test 11, Cortex-M targets only)
// --------------------------------------------------------------------------
// A real timer interrupt (bsp.h) re-arms a one-shot dmq::util::Timer with
// Start(), and starts and stops a second one, while a task does the same to
// other timers and the timer daemon task runs ProcessTimers(). Start() and
// Stop() take Timer::GetLock(), a dmq::CriticalSection: on FreeRTOS,
// FreeRTOSCriticalSection, which must pick taskENTER_CRITICAL_FROM_ISR() in
// the interrupt. taskENTER_CRITICAL() there would trip the Cortex-M port's
// configASSERT (it checks it isn't called from an interrupt), ending the run.
static constexpr uint32_t ISR_HZ = 2000;          // interrupt every 0.5 ms
// Re-arm the 5 ms one-shot every 34 interrupts (17 ms): ProcessTimers() runs
// every 10 ms, so each expiry is seen (within 15 ms) before the next re-arm
static constexpr uint32_t ISR_REARM_EVERY = 34;
static Timer* s_isrOneShot = nullptr;             // started from the ISR, fires in the daemon task
static Timer* s_isrChurn = nullptr;               // started and stopped from the ISR
static volatile uint32_t s_isrCount = 0;
static volatile uint32_t s_isrNotInInterrupt = 0; // ISR entries that FreeRTOS didn't see as an interrupt
static std::atomic<int> s_isrOneShotFired{0};

static void OnIsrOneShot() { s_isrOneShotFired++; }

// Test 12: an IsrDelegate posted from the timer ISR. Its target is async, to a
// worker thread. A drain task runs IsrDelegates::ProcessAll(), woken by the
// post handler, which gives its semaphore from the ISR. Each post carries a
// sequence number and whether FreeRTOS saw an interrupt.
static IsrDelegate<void(uint32_t, bool)>* s_isrPost = nullptr;
static volatile uint32_t s_isrPostSeq = 0;
static Thread* s_isrPostThread = nullptr;
static uint32_t s_isrPostLast = 0;          // written only on the worker thread
static volatile uint32_t s_isrPostReceived = 0;
static uint32_t s_isrPostGaps = 0;          // sequence numbers skipped (dropped posts)
static uint32_t s_isrPostBad = 0;           // out of order, not from an ISR, or wrong thread
static SemaphoreHandle_t s_drainSem = nullptr;
static volatile bool s_drainPaused = false;
static volatile bool s_drainRun = false;

static void PostFromIsr() {
    s_isrPostSeq = s_isrPostSeq + 1;
    s_isrPost->Post(s_isrPostSeq, xPortIsInsideInterrupt() != pdFALSE);
}

// IsrDelegates post handler: called by Post() in the ISR
static void WakeDrainTask() {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_drainSem, &woken);
    portYIELD_FROM_ISR(woken);
}

static void DrainTask(void*) {
    while (s_drainRun) {
        xSemaphoreTake(s_drainSem, pdMS_TO_TICKS(10));
        if (!s_drainPaused)
            IsrDelegates::ProcessAll();
    }
    vTaskDelete(nullptr);
}

static void OnIsrPost(uint32_t seq, bool fromIsr) {
    if (seq <= s_isrPostLast || !fromIsr || !s_isrPostThread->IsCurrentThread())
        s_isrPostBad++;
    else
        s_isrPostGaps += seq - s_isrPostLast - 1;
    s_isrPostLast = seq;
    s_isrPostReceived = s_isrPostReceived + 1;
}

// Test 14: no heap once running. The target builds with DMQ_ALLOCATOR in static
// pools (target.cmake) and counts every malloc/calloc/realloc (main.c). After
// startup, async calls, signal emits, async-wait calls, timer ticks and ISR
// posts must not change the count. The targets below run on a worker thread.
static std::atomic<uint32_t> s_noHeapCalls{0};
static std::atomic<uint32_t> s_noHeapSum{0};
static void NoHeapTarget(int value) { s_noHeapCalls++; s_noHeapSum += static_cast<uint32_t>(value); }
static int NoHeapSquare(int value) { return value * value; }
static void NoHeapTick() { s_noHeapCalls++; }

// Test 15: the watchdog hook (dmq::os::SetWatchdogHook()). Two threads hang:
// one blocked forever on a semaphore (as in a deadlock), one spinning in
// SpinForever(). A checker task calls Thread::WatchdogCheckAll(). The hook
// records each hung thread and the program counter FreeRTOS saved for it, which
// shows where it's stuck. Then WatchdogHandler() (targets/arm-m3/fault.cpp)
// calls OnWatchdogExpected(), which checks the records and ends the run.
struct HungRecord { const char* name; uint32_t stalledMs; uint32_t pc; };
static HungRecord s_hung[4];
static volatile uint32_t s_hungCount = 0;
static volatile bool s_spin = true;
static SemaphoreHandle_t s_neverGiven = nullptr;

__attribute__((noinline)) static void SpinForever() { while (s_spin) { } }
static void BlockForever() { xSemaphoreTake(s_neverGiven, portMAX_DELAY); }

// The PC a suspended task resumes at. FreeRTOS ARM_CM3 port: the TCB starts with
// pxTopOfStack, where PendSV saved r4-r11 below the exception frame (r0-r3, r12,
// lr, pc, xPSR), so the PC is word 8 + 6.
static uint32_t SavedPc(TaskHandle_t task) {
    const uint32_t* sp = *reinterpret_cast<uint32_t* const*>(task);
    return sp[8 + 6];
}

// The watchdog hook: runs in the checker task, under the watchdog list lock.
// Records only (no printf: it may allocate); OnWatchdogExpected() prints.
static void RecordHungThread(const WatchdogInfo& info) {
    if (s_hungCount < 4) {
        s_hung[s_hungCount] = { info.threadName,
            static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(info.stalledFor).count()),
            SavedPc(info.thread.GetThreadId()) };
        s_hungCount = s_hungCount + 1;
    }
}

static void WatchdogTask(void*) {
    for (;;) {
        Thread::WatchdogCheckAll();     // a hung thread: never returns
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// WatchdogHandler(), after the hook: check what it recorded and end the run
static void OnWatchdogExpected(const char* threadName) {
    printf("  -> WatchdogHandler(\"%s\") after the hook saw %lu hung threads:\n",
           threadName, static_cast<unsigned long>(s_hungCount));
    const uint32_t spin = reinterpret_cast<uint32_t>(&SpinForever) & ~1u;     // without the Thumb bit
    bool spinAtPc = false;
    bool blocked = false;
    for (uint32_t i = 0; i < s_hungCount; i++) {
        printf("     %s: stalled %lu ms, saved PC 0x%08lx\n", s_hung[i].name,
               static_cast<unsigned long>(s_hung[i].stalledMs), static_cast<unsigned long>(s_hung[i].pc));
        DMQ_ASSERT_TRUE(s_hung[i].stalledMs > 200);
        if (strcmp(s_hung[i].name, "HungSpin") == 0)
            spinAtPc = s_hung[i].pc >= spin && s_hung[i].pc < spin + 64;
        if (strcmp(s_hung[i].name, "HungBlocked") == 0)
            blocked = true;
    }
    DMQ_ASSERT_TRUE(s_hungCount == 2 && blocked && spinAtPc);
    DMQ_ASSERT_TRUE(strcmp(threadName, s_hung[0].name) == 0);    // the first one reported
    printf("  -> both hung threads reported, then one fault; HungSpin's saved PC is in SpinForever()\n");
    PrintAllTestsPassed();
    BspExit(0);
}

static void TimerIsr() {
    s_isrCount = s_isrCount + 1;
    if (xPortIsInsideInterrupt() == pdFALSE)
        s_isrNotInInterrupt = s_isrNotInInterrupt + 1;
    if (s_isrCount % ISR_REARM_EVERY == 0)
        s_isrOneShot->Start(std::chrono::milliseconds(5), true);
    if (s_isrCount % 2)
        s_isrChurn->Start(std::chrono::milliseconds(1000));
    else
        s_isrChurn->Stop();
}
#endif

// --------------------------------------------------------------------------
// CALLBACK FUNCTIONS
// --------------------------------------------------------------------------
void FreeFunction(int val) {
    printf("  [Callback] FreeFunction: %d (Task: %s)\n", val, pcTaskGetName(NULL));
}

class TestHandler {
public:
    void MemberFunc(int val) {
        printf("  [Callback] MemberFunc: %d (Task: %s, Instance: %p)\n", val, pcTaskGetName(NULL), this);
    }

    void OnTimerExpired() {
        printf("  [Callback] Timer Expired! (Task: %s)\n", pcTaskGetName(NULL));
    }
};

// --------------------------------------------------------------------------
// TEST LOGIC (Moved to helper function)
// --------------------------------------------------------------------------
void ExecuteAllTests() {
    // 1. Critical: Disable buffering
    setvbuf(stdout, NULL, _IONBF, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    printf("\n=========================================\n");
    printf("   FREERTOS DELEGATE SYSTEM ONLINE       \n");
    printf("=========================================\n");

    // --- TEST 1: Unicast Delegate ---
    printf("\n[Test 1] Unicast Delegate (Free Function):\n");
    UnicastDelegate<void(int)> unicast;
    unicast = MakeDelegate(FreeFunction);
    unicast(100);

    // --- TEST 2: Lambda Support ---
    printf("\n[Test 2] Unicast Delegate (Lambda):\n");
    int capture_value = 42;
    unicast = MakeDelegate(std::function<void(int)>([capture_value](int val) {
        printf("  [Callback] Lambda called! Capture: %d, Arg: %d\n", capture_value, val);
        }));
    unicast(200);

    // --- TEST 3: Multicast Delegate ---
    printf("\n[Test 3] Multicast Delegate (Broadcast):\n");
    MulticastDelegate<void(int)> multicast;
    TestHandler handler;

    multicast += MakeDelegate(FreeFunction);
    multicast += MakeDelegate(&handler, &TestHandler::MemberFunc);

    multicast += MakeDelegate(std::function<void(int)>([](int val) {
        printf("  [Callback] Multicast Lambda called! Val: %d\n", val);
        }));

    printf("Firing all 3 targets...\n");
    multicast(300);

    // --- TEST 4: Removal ---
    printf("\n[Test 4] Removing a Delegate:\n");
    multicast -= MakeDelegate(FreeFunction);
    printf("Firing remaining targets (Expected: 2)...\n");
    multicast(400);

    // --- TEST 5: Signals & Connections (RAII) ---
    printf("\n[Test 5] Signals & Scoped Connections:\n");
    Signal<void(int)> signal;

    {
        printf("  -> Creating ScopedConnection inside block...\n");
        ScopedConnection conn = signal.Connect(MakeDelegate(FreeFunction));

        printf("  -> Firing Signal (Expect Callback):\n");
        signal(500);

        printf("  -> Exiting block (ScopedConnection will destruct)...\n");
    }
    printf("  -> Firing Signal outside block (Expect NO Callback):\n");
    signal(600);

    // --- TEST 6: Thread-Safe Delegates ---
    printf("\n[Test 6] Thread-Safe Multicast (Mutex Protected):\n");
    MulticastDelegateSafe<void(int)> safeMulticast;
    safeMulticast += MakeDelegate(FreeFunction);

    printf("Firing Thread-Safe Delegate...\n");
    safeMulticast(700);

    // --- TEST 7: Async Delegates (Cross-Thread) ---
    printf("\n[Test 7] Async Delegate (Cross-Thread Dispatch):\n");

    // Create a worker thread (Active Object)
    // NOTE: C++ Destructor ~Thread() MUST run to close the queue properly!
    Thread workerThread("WorkerThread");
    if (workerThread.CreateThread()) {

        auto asyncDelegate = MakeDelegate(FreeFunction, workerThread);

        printf("  -> Dispatching to Worker Thread (Non-Blocking)...\n");
        asyncDelegate(800);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
    else {
        printf("  [Error] Failed to create WorkerThread!\n");
    }

    // --- TEST 8: Timers ---
    printf("\n[Test 8] Timer Delegate (One-Shot):\n");

    // NOTE: C++ Destructor ~Timer() MUST run to unregister from the global list!
    Timer myTimer;

    ScopedConnection timerConn = myTimer.OnExpired.Connect(MakeDelegate(&handler, &TestHandler::OnTimerExpired));

    printf("  -> Starting Timer (200ms delay)...\n");
    myTimer.Start(std::chrono::milliseconds(200));

    // Wait for timer
    vTaskDelay(pdMS_TO_TICKS(300));
    myTimer.Stop();

    // --- TEST 9: Start Gate ---
    printf("\n[Test 9] Start Gate (hold threads until one go):\n");
    {
        // Two threads held at one gate: ready, but nothing runs at any priority
        StartGate gate;
        Thread g1("Gated1");
        Thread g2("Gated2");
        g1.SetStartGate(&gate);
        g2.SetStartGate(&gate);
        DMQ_ASSERT_TRUE(g1.CreateThread() && g2.CreateThread());
        s_gateRan = 0;
        for (Thread* t : { &g1, &g2 }) {
            MakeDelegate(&GateCount, *t)(0);
            auto high = MakeDelegate(&GateCount, *t);
            high.SetPriority(Priority::URGENT);
            high(0);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        DMQ_ASSERT_TRUE(s_gateRan == 0);
        printf("  -> 4 messages queued to 2 gated threads, none run\n");
        gate.Open();
        vTaskDelay(pdMS_TO_TICKS(100));
        DMQ_ASSERT_TRUE(s_gateRan == 4);
        printf("  -> gate opened: all 4 ran\n");
        g1.ExitThread();
        g2.ExitThread();

        // Exit while held, queue full: ExitThread() must not block on the full
        // queue, and nothing runs
        StartGate closed;
        Thread g3("Gated3", 4, FullPolicy::DROP);
        g3.SetStartGate(&closed);
        DMQ_ASSERT_TRUE(g3.CreateThread());
        s_gateRan = 0;
        for (int i = 0; i < 6; i++)
            MakeDelegate(&GateCount, g3)(0);   // 4 queued, 2 dropped
        g3.ExitThread();
        DMQ_ASSERT_TRUE(s_gateRan == 0);
        printf("  -> exit while held (full queue): returned, nothing ran\n");

        // Thread created before the scheduler started (see main_delegate())
        DMQ_ASSERT_TRUE(s_earlyStarted);
        s_gateRan = 0;
        MakeDelegate(&GateCount, *s_earlyThread)(0);
        vTaskDelay(pdMS_TO_TICKS(50));
        DMQ_ASSERT_TRUE(s_gateRan == 0);
        s_earlyGate->Open();
        vTaskDelay(pdMS_TO_TICKS(50));
        DMQ_ASSERT_TRUE(s_gateRan == 1);
        s_earlyThread->ExitThread();
        printf("  -> thread created before the scheduler: held, then ran\n");

        // The portable gate tests every port runs (test/unit-tests)
        StartGatePortableTest::Run();
    }

    // --- TEST 10: Self-exit from the idle handler ---
    // The idle handler exits its own thread and deletes it (allowed on
    // FreeRTOS: the idle task frees the task's stack later). The loop must
    // return without touching the destroyed Thread.
    printf("\n[Test 10] Idle handler exits and deletes its own thread:\n");
    {
        static std::atomic<bool> s_idleDone{false};
        s_idleDone = false;
        Thread* self = new Thread("IdleSelfDelete");
        self->SetIdleHandler(MakeDelegate(std::function<void()>([self]() {
            self->ExitThread();
            delete self;
            s_idleDone = true;
        })), std::chrono::milliseconds(20));
        DMQ_ASSERT_TRUE(self->CreateThread());
        for (int i = 0; i < 100 && !s_idleDone; i++)
            vTaskDelay(pdMS_TO_TICKS(10));
        DMQ_ASSERT_TRUE(s_idleDone);
        vTaskDelay(pdMS_TO_TICKS(50));      // let the task finish and the idle task free it
        printf("  -> thread exited and deleted itself from its idle handler\n");
    }

#if defined(BSP_TARGET)
    // --- TEST 11: Timer API from a hardware ISR (see TimerIsr()) ---
    printf("\n[Test 11] Timer Start/Stop from a hardware timer ISR:\n");
    {
        Timer oneShot;
        Timer churn;
        ScopedConnection conn = oneShot.OnExpired.Connect(MakeDelegate(&OnIsrOneShot));
        s_isrOneShot = &oneShot;
        s_isrChurn = &churn;
        s_isrCount = 0;
        s_isrNotInInterrupt = 0;
        s_isrOneShotFired = 0;

        BspStartTimerIsr(ISR_HZ, &TimerIsr);

        // Meanwhile, the same lock from task context: timers created, started,
        // stopped and destroyed here, contending with the interrupt
        const TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(500);
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

        BspStopTimerIsr();
        vTaskDelay(pdMS_TO_TICKS(50));   // let a last one-shot expire and fire
        oneShot.Stop();
        churn.Stop();

        const uint32_t isrCount = s_isrCount;
        const int fired = s_isrOneShotFired;
        printf("  -> %lu interrupts, %u task-side loops, one-shot fired %d times\n",
               static_cast<unsigned long>(isrCount), taskOps, fired);
        // ~1000 interrupts in 500 ms on a Linux host. QEMU on Windows delivers
        // its timers at the host's ~15 ms granularity, so fewer interrupts
        // there (coalesced, as a masked interrupt pending twice is on real
        // hardware) and a longer run: the checks don't assume a rate.
        DMQ_ASSERT_TRUE(isrCount >= 100);
        DMQ_ASSERT_TRUE(s_isrNotInInterrupt == 0);        // FreeRTOS saw every one as an interrupt
        DMQ_ASSERT_TRUE(taskOps > 0);
        // Each re-arm (5 ms one-shot) normally fires before the next one, but
        // a re-arm that lands before ProcessTimers() saw the expiry restarts it
        const int rearms = static_cast<int>(isrCount / ISR_REARM_EVERY);
        DMQ_ASSERT_TRUE(fired >= rearms / 2 && fired <= rearms + 1);
        printf("  -> ISR-context critical sections ran without a configASSERT\n");
    }

    // --- TEST 12: IsrDelegate: a hardware ISR calls a function on a thread ---
    printf("\n[Test 12] IsrDelegate posted from a hardware timer ISR to a worker thread:\n");
    {
        Thread worker("IsrPostWorker");
        DMQ_ASSERT_TRUE(worker.CreateThread());
        IsrDelegate<void(uint32_t, bool)> isrPost(MakeDelegate(&OnIsrPost, worker));
        s_isrPost = &isrPost;
        s_isrPostThread = &worker;

        s_drainSem = xSemaphoreCreateBinary();
        s_drainRun = true;
        xTaskCreate(DrainTask, "IsrDrain", 1024, nullptr, 3, nullptr);
        IsrDelegates::SetPostHandler(&WakeDrainTask);

        BspStartTimerIsr(ISR_HZ, &PostFromIsr);
        vTaskDelay(pdMS_TO_TICKS(500));
        BspStopTimerIsr();
        vTaskDelay(pdMS_TO_TICKS(50));      // the last posts reach the worker

        const uint32_t posted = s_isrPostSeq;
        const size_t dropped = isrPost.GetDroppedCount();
        printf("  -> %lu posted from the ISR, %lu called on the worker, %lu dropped (ring full)\n",
               static_cast<unsigned long>(posted), static_cast<unsigned long>(s_isrPostReceived),
               static_cast<unsigned long>(dropped));
        DMQ_ASSERT_TRUE(posted >= 100);
        DMQ_ASSERT_TRUE(s_isrPostBad == 0);                      // in order, from the ISR, on the worker
        DMQ_ASSERT_TRUE(s_isrPostReceived + dropped == posted);  // nothing lost or doubled

        // Drain paused: the ring fills, and the posts it can't hold are
        // dropped and counted, never blocking the ISR
        s_drainPaused = true;
        vTaskDelay(pdMS_TO_TICKS(20));
        BspStartTimerIsr(ISR_HZ, &PostFromIsr);
        vTaskDelay(pdMS_TO_TICKS(50));
        BspStopTimerIsr();
        s_drainPaused = false;
        vTaskDelay(pdMS_TO_TICKS(50));
        const uint32_t posted2 = s_isrPostSeq;
        const size_t dropped2 = isrPost.GetDroppedCount();
        printf("  -> drain paused: %lu posted, %lu called, %lu dropped\n",
               static_cast<unsigned long>(posted2), static_cast<unsigned long>(s_isrPostReceived),
               static_cast<unsigned long>(dropped2));
        DMQ_ASSERT_TRUE(dropped2 > dropped);
        DMQ_ASSERT_TRUE(s_isrPostBad == 0);
        DMQ_ASSERT_TRUE(s_isrPostReceived + dropped2 == posted2);

        // The same IsrDelegate from task context. It also closes the gap left
        // by the last drops: the ring keeps the oldest posts, drops the newest
        const uint32_t before = s_isrPostReceived;
        s_isrPostSeq = s_isrPostSeq + 1;
        DMQ_ASSERT_TRUE(isrPost.Post(s_isrPostSeq, true));
        vTaskDelay(pdMS_TO_TICKS(30));
        DMQ_ASSERT_TRUE(s_isrPostReceived == before + 1);
        DMQ_ASSERT_TRUE(s_isrPostBad == 0);
        DMQ_ASSERT_TRUE(s_isrPostGaps == isrPost.GetDroppedCount());   // each gap a counted drop

        IsrDelegates::SetPostHandler(nullptr);
        s_drainRun = false;
        vTaskDelay(pdMS_TO_TICKS(30));      // the drain task exits
        vSemaphoreDelete(s_drainSem);
        s_drainSem = nullptr;
        worker.ExitThread();
        printf("  -> every post ran once, in order, on the worker thread\n");
    }
#endif

    // --- TEST 13: Event-driven timers: ProcessTimers() only when one is due ---
    printf("\n[Test 13] Event-driven timers (GetNextExpiry, no polling period):\n");
    {
        using namespace std::chrono;

        // Stop the 10 ms driver: from here only TimerDriverTask runs ProcessTimers()
        xTimerStop(xSystemTimer, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(20));

        Timer a;
        Timer b;
        ScopedConnection connA = a.OnExpired.Connect(MakeDelegate(&OnDriverTimerA));
        ScopedConnection connB = b.OnExpired.Connect(MakeDelegate(&OnDriverTimerB));

        // GetNextExpiry(), with nothing processing timers. No other timer runs
        // now, so the values are exact.
        DMQ_ASSERT_TRUE(Timer::GetNextExpiry() == dmq::Duration::max());
        a.Start(milliseconds(500), true);
        dmq::Duration next = Timer::GetNextExpiry();
        DMQ_ASSERT_TRUE(next > milliseconds(490) && next <= milliseconds(500));
        b.Start(milliseconds(100), true);
        next = Timer::GetNextExpiry();
        DMQ_ASSERT_TRUE(next > milliseconds(90) && next <= milliseconds(100));   // the earliest
        b.Stop();
        next = Timer::GetNextExpiry();
        DMQ_ASSERT_TRUE(next > milliseconds(400) && next <= milliseconds(500));  // back to a
        vTaskDelay(pdMS_TO_TICKS(550));
        DMQ_ASSERT_TRUE(Timer::GetNextExpiry() == dmq::Duration(0));             // overdue
        a.Stop();
        DMQ_ASSERT_TRUE(Timer::GetNextExpiry() == dmq::Duration::max());
        DMQ_ASSERT_TRUE(s_firedA == 0 && s_firedB == 0);
        printf("  -> GetNextExpiry(): none, earliest of two, overdue: as expected\n");

        // The driver, with no timer running: it waits without waking
        s_driverWakes = 0;
        s_driverRun = true;
        xTaskCreate(TimerDriverTask, "TimerDriver", 2048, nullptr, configMAX_PRIORITIES - 2, &s_driverTask);
        Timer::SetScheduleChangedHandler(&WakeTimerDriver);
        vTaskDelay(pdMS_TO_TICKS(200));
        DMQ_ASSERT_TRUE(s_driverWakes == 0);

        // A one-shot: Start() wakes the driver, which then waits until it's due
        TickType_t start = xTaskGetTickCount();
        a.Start(milliseconds(50), true);
        vTaskDelay(pdMS_TO_TICKS(100));
        DMQ_ASSERT_TRUE(s_firedA == 1);
        TickType_t late = s_tickA - start - pdMS_TO_TICKS(50);
        printf("  -> 50 ms one-shot fired %lu ms late; %lu driver wake-ups in 300 ms (a 10 ms poll: 30)\n",
               static_cast<unsigned long>(late), static_cast<unsigned long>(s_driverWakes));
        DMQ_ASSERT_TRUE(late <= 3);
        DMQ_ASSERT_TRUE(s_driverWakes <= 3);         // Start(), then the deadline

        // A sooner timer started while the driver waits for a later one: the
        // handler wakes it, so it doesn't sleep through the new deadline
        a.Start(milliseconds(1000), true);
        vTaskDelay(pdMS_TO_TICKS(20));
        start = xTaskGetTickCount();
        b.Start(milliseconds(30), true);
        vTaskDelay(pdMS_TO_TICKS(60));
        DMQ_ASSERT_TRUE(s_firedB == 1);
        late = s_tickB - start - pdMS_TO_TICKS(30);
        printf("  -> 30 ms one-shot, started during a 1000 ms wait, fired %lu ms late\n",
               static_cast<unsigned long>(late));
        DMQ_ASSERT_TRUE(late <= 3);
        a.Stop();

        // A periodic timer: wake-ups follow its expiries, not a polling period
        const uint32_t wakes0 = s_driverWakes;
        const uint32_t fired0 = s_firedA;
        a.Start(milliseconds(100));
        vTaskDelay(pdMS_TO_TICKS(1050));
        a.Stop();
        const uint32_t fired = s_firedA - fired0;
        const uint32_t wakes = s_driverWakes - wakes0;
        printf("  -> 100 ms periodic for 1050 ms: %lu expiries, %lu driver wake-ups (a 10 ms poll: 105)\n",
               static_cast<unsigned long>(fired), static_cast<unsigned long>(wakes));
        DMQ_ASSERT_TRUE(fired >= 9 && fired <= 11);
        DMQ_ASSERT_TRUE(wakes <= fired + 2);

        // Back to the 10 ms software timer
        Timer::SetScheduleChangedHandler(nullptr);
        s_driverRun = false;
        xTaskNotifyGive(s_driverTask);
        vTaskDelay(pdMS_TO_TICKS(20));      // the driver task exits
        s_driverTask = nullptr;
        xTimerStart(xSystemTimer, portMAX_DELAY);
    }

#if defined(BSP_TARGET)
    // --- TEST 14: No heap once the system is running ---
    printf("\n[Test 14] Steady state uses no heap (static pools, heap calls counted):\n");
    {
        // Startup: may allocate (thread, connections, timer)
        Thread worker("NoHeapWorker");
        DMQ_ASSERT_TRUE(worker.CreateThread());
        auto async = MakeDelegate(&NoHeapTarget, worker);
        auto asyncWait = MakeDelegate(&NoHeapSquare, worker, WAIT_INFINITE);
        Signal<void(int)> signal;
        ScopedConnection conn = signal.Connect(MakeDelegate(&NoHeapTarget, worker));
        Timer timer;
        ScopedConnection timerConn = timer.OnExpired.Connect(MakeTimerDelegate(&NoHeapTick, worker));
        IsrDelegate<void(int)> isrPost(MakeDelegate(&NoHeapTarget, worker));
        s_noHeapCalls = 0;
        s_noHeapSum = 0;

        // Running: nothing below may call the heap
        const uint32_t heapBefore = BspHeapCallCount();
        timer.Start(std::chrono::milliseconds(10));
        uint32_t expectSum = 0;
        int waitOk = 0;
        for (int i = 1; i <= 500; i++) {
            async(i);
            signal(i);
            expectSum += 2u * static_cast<uint32_t>(i);
            if (i % 5 == 0) {
                if (isrPost.Post(i))
                    expectSum += static_cast<uint32_t>(i);
                IsrDelegates::ProcessAll();
            }
            if (i % 10 == 0) {
                auto ret = asyncWait.AsyncInvoke(i);
                if (ret.has_value() && ret.value() == i * i)
                    waitOk++;
            }
            if (i % 50 == 0)
                vTaskDelay(pdMS_TO_TICKS(20));      // let the worker and the timer run
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        timer.Stop();
        vTaskDelay(pdMS_TO_TICKS(50));
        const uint32_t heapCalls = BspHeapCallCount() - heapBefore;

        printf("  -> %lu calls ran on the worker (async, signal, ISR posts, timer ticks), %d async-wait results\n",
               static_cast<unsigned long>(s_noHeapCalls.load()), waitOk);
        printf("  -> heap calls while running: %lu\n", static_cast<unsigned long>(heapCalls));
        if (heapCalls != 0) {
            size_t size = 0;
            void* caller = BspHeapLastCaller(&size);
            printf("  -> last one: %lu bytes, called from %p (arm-none-eabi-addr2line -f -e delegate_app)\n",
                   static_cast<unsigned long>(size), caller);
        }
        printf("  -> the fixed-block pools' peak use so far (xalloc_print_pool_config()):\n");
        xalloc_print_pool_config();
        DMQ_ASSERT_TRUE(waitOk == 50);
        DMQ_ASSERT_TRUE(s_noHeapCalls >= 1000 + 100 + 10);  // async + signal, ISR posts, some ticks
        DMQ_ASSERT_TRUE(s_noHeapSum == expectSum);           // every async, signal and ISR call ran once
        DMQ_ASSERT_TRUE(heapCalls == 0);
        worker.ExitThread();
    }

    // --- TEST 15: Watchdog hook (last: a watchdog expiry ends the run) ---
    printf("\n[Test 15] Watchdog hook: two hung threads reported, then one fault:\n");
    {
        static Thread blocked("HungBlocked");
        static Thread spinning("HungSpin");
        s_neverGiven = xSemaphoreCreateBinary();
        spinning.SetThreadPriority(1);      // below this task (2), which keeps running
        DMQ_ASSERT_TRUE(blocked.CreateThread(std::chrono::milliseconds(200)));
        DMQ_ASSERT_TRUE(spinning.CreateThread(std::chrono::milliseconds(200)));
        SetWatchdogHook(&RecordHungThread);
        BspWatchdogExpected = &OnWatchdogExpected;
        MakeDelegate(&BlockForever, blocked)();
        MakeDelegate(&SpinForever, spinning)();

        // Both stall for more than 200 ms before the first check, so one
        // WatchdogCheckAll() pass finds both
        vTaskDelay(pdMS_TO_TICKS(300));
        xTaskCreate(WatchdogTask, "Watchdog", 1024, nullptr, configMAX_PRIORITIES - 1, nullptr);
        vTaskDelay(pdMS_TO_TICKS(5000));
        printf("  [Error] the watchdog didn't expire\n");
        BspExit(3);
    }
#endif

    PrintAllTestsPassed();

    // FUNCTION RETURN:
    // This closing brace '}' forces ~Timer(), ~Thread(), etc. to execute.
    // This safely unregisters the Timer before the task is deleted.
}

static void PrintAllTestsPassed() {
    printf("\n=========================================\n");
    printf("           ALL TESTS PASSED              \n");
    printf("=========================================\n");
}

// --------------------------------------------------------------------------
// TEST RUNNER TASK
// --------------------------------------------------------------------------
void RunTestsTask(void* pvParameters) {

    // Run all tests in a function to ensure stack unwinding happens
    ExecuteAllTests();

#if defined(BSP_TARGET)
    // On QEMU: end the run with status 0 (a failed check faults before here)
    BspExit(0);
#endif

    // Now it is safe to delete the task because 'myTimer' is destroyed.
    vTaskDelete(NULL);
}

// --------------------------------------------------------------------------
// MAIN ENTRY POINT
// --------------------------------------------------------------------------
extern "C" void main_delegate(void)
{
    xSystemTimer = xTimerCreate("SysTimer", mainTIMER_FREQUENCY_MS, pdTRUE, NULL, TimerCallback);
    xTimerStart(xSystemTimer, 0);

    xTaskCreate(RunTestsTask, "MainTask", 2048, NULL, 2, NULL);

    // Test 9: a gated thread created before the scheduler starts
    static StartGate earlyGate;
    static Thread earlyThread("EarlyGatedThread");
    s_earlyGate = &earlyGate;
    s_earlyThread = &earlyThread;
    earlyThread.SetStartGate(&earlyGate);
    earlyThread.SetStartHandler(MakeDelegate(+[]() { s_earlyStarted = true; }));
    earlyThread.CreateThread();

    printf("--- Starting FreeRTOS Scheduler ---\n");
    vTaskStartScheduler();

    while (1);
}