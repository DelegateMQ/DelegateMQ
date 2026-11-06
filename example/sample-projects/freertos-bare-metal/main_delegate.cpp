/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.

#include "DelegateMQ.h" 
#include "../../../test/unit-tests/StartGatePortableTest.h"
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

// --------------------------------------------------------------------------
// FREERTOS CONFIGURATION & HELPERS
// --------------------------------------------------------------------------
#define mainTIMER_FREQUENCY_MS pdMS_TO_TICKS(10UL)
static TimerHandle_t xSystemTimer = nullptr;

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
    // 
    Timer::ProcessTimers();
}

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
            high.SetPriority(Priority::HIGH);
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

    printf("\n=========================================\n");
    printf("           ALL TESTS PASSED              \n");
    printf("=========================================\n");

    // FUNCTION RETURN:
    // This closing brace '}' forces ~Timer(), ~Thread(), etc. to execute.
    // This safely unregisters the Timer before the task is deleted.
}

// --------------------------------------------------------------------------
// TEST RUNNER TASK
// --------------------------------------------------------------------------
void RunTestsTask(void* pvParameters) {

    // Run all tests in a function to ensure stack unwinding happens
    ExecuteAllTests();

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