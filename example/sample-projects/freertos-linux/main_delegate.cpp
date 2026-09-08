/// @file main_delegate.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief DelegateMQ feature demo running on FreeRTOS (POSIX/Linux simulation port).
///
/// Identical test suite to freertos-bare-metal (the Win32-simulator sample)
/// -- same delegate, Signal, ScopedConnection, Thread, and Timer usage --
/// showing the application code is unchanged between the two FreeRTOS
/// simulator ports; only the build target changes. The periodic software
/// timer below exercises dmq::util::Timer::ProcessTimers() through
/// dmq::os::FreeRTOSCriticalSection's task-context path (see
/// port/os/freertos/FreeRTOSCriticalSection.h) on every tick.

#include "DelegateMQ.h"
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// --------------------------------------------------------------------------
// FREERTOS CONFIGURATION & HELPERS
// --------------------------------------------------------------------------
#define mainTIMER_FREQUENCY_MS pdMS_TO_TICKS(10UL)
static TimerHandle_t xSystemTimer = nullptr;

static void TimerCallback(TimerHandle_t /*xTimerHandle*/) {
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
        printf("  [Callback] MemberFunc: %d (Task: %s, Instance: %p)\n", val, pcTaskGetName(NULL), (void*)this);
    }

    void OnTimerExpired() {
        printf("  [Callback] Timer Expired! (Task: %s)\n", pcTaskGetName(NULL));
    }
};

// --------------------------------------------------------------------------
// TEST LOGIC
// --------------------------------------------------------------------------
void ExecuteAllTests() {
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
void RunTestsTask(void* /*pvParameters*/) {

    // Run all tests in a function to ensure stack unwinding happens
    ExecuteAllTests();

    // POSIX FreeRTOS simulator: the scheduler runs as the host process, and
    // vTaskDelete(NULL) on the calling task never returns control here -- so
    // exit the whole process directly instead (same approach threadx-linux
    // uses), rather than deleting the task and leaving the idle/timer tasks
    // running forever with nothing left to demonstrate.
    std::exit(0);
}

// --------------------------------------------------------------------------
// MAIN ENTRY POINT
// --------------------------------------------------------------------------
extern "C" void main_delegate(void)
{
    xSystemTimer = xTimerCreate("SysTimer", mainTIMER_FREQUENCY_MS, pdTRUE, NULL, TimerCallback);
    xTimerStart(xSystemTimer, 0);

    xTaskCreate(RunTestsTask, "MainTask", 2048, NULL, 2, NULL);

    printf("--- Starting FreeRTOS Scheduler (POSIX simulation) ---\n");
    vTaskStartScheduler();

    while (1);
}
