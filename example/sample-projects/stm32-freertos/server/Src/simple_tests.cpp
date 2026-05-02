/**
  * @file simple_tests.cpp
  * @brief Basic sanity checks for DelegateMQ functionality.
  */
#include "DelegateMQ.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// --- Test Handlers ---
static void FreeFunction(int val) {
    printf("  [Simple] FreeFunction: %d (Task: %s)\n", val, pcTaskGetName(NULL));
}

class SimpleHandler {
public:
    void MemberFunc(int val) {
        printf("  [Simple] MemberFunc: %d (Task: %s)\n", val, pcTaskGetName(NULL));
    }

    void OnTimerExpired() { 
        printf("  [Simple] Timer Expired! (Task: %s)\n", pcTaskGetName(NULL));
        BSP_LED_Toggle(LED3); 
    }
};

// --- Test Entry Point ---
void StartSimpleTests() {
    printf("\n=== STARTING SIMPLE TESTS ===\n");
    printf("Current Task: %s\n", pcTaskGetName(NULL));

    SimpleHandler handler;

    // --------------------------------------------------------
    // 1. Unicast (Synchronous)
    // --------------------------------------------------------
    printf("\n[1] Unicast (Sync)\n");
    // MakeDelegate automatically picks up the function pointer type
    UnicastDelegate<void(int)> unicast = MakeDelegate(FreeFunction);
    unicast(100);

    // --------------------------------------------------------
    // 2. Multicast (Synchronous)
    // --------------------------------------------------------
    printf("\n[2] Multicast (Sync)\n");
    MulticastDelegate<void(int)> multicast;
    multicast += MakeDelegate(&handler, &SimpleHandler::MemberFunc);
    multicast += MakeDelegate(FreeFunction);
    multicast(200);

    // --------------------------------------------------------
    // 3. Thread-Safe (Synchronous)
    // --------------------------------------------------------
    printf("\n[3] Thread-Safe (Sync)\n");
    MulticastDelegateSafe<void(int)> safeMulticast;
    safeMulticast += MakeDelegate(FreeFunction);
    safeMulticast(300);

    // --------------------------------------------------------
    // 4. Timer (Synchronous via Timer Task)
    // --------------------------------------------------------
    printf("\n[4] Timer (Wait 200ms)\n");
    Timer myTimer;
    dmq::ScopedConnection timerConn = myTimer.OnExpired.Connect(MakeDelegate(&handler, &SimpleHandler::OnTimerExpired));
    myTimer.Start(std::chrono::milliseconds(200));
    vTaskDelay(pdMS_TO_TICKS(250));

    // --------------------------------------------------------
    // 5. Signals & Slots
    // --------------------------------------------------------
    printf("\n[5] Signals & Slots\n");
    Signal<void(int)> signal;
    {
        ScopedConnection conn = signal.Connect(MakeDelegate(FreeFunction));
        signal(555);
    }
    signal(666); // Should NOT fire

    // --------------------------------------------------------
    // 6. ASYNC DELEGATES (Cross-Thread Dispatch)
    // --------------------------------------------------------
    printf("\n[6] Async Delegates (Worker Thread)\n");

    // Worker Thread (Platform specific wrapper)
    Thread worker("Worker_Thread", 10);

    if (worker.CreateThread()) {
        printf("  >> Worker Thread Created.\n");
        vTaskDelay(pdMS_TO_TICKS(50));

        // Test A: Async Free Function
        // MakeDelegate detects (void(*)(int), IThread&) -> Returns DelegateAsync
        printf("  >> Dispatching FreeFunction (Arg=777)...\n");
        auto d1 = MakeDelegate(FreeFunction, worker);
        d1.AsyncInvoke(777);

        // Test B: Async Member Function
        // MakeDelegate detects (Obj*, Func*, IThread&) -> Returns DelegateMemberAsync
        printf("  >> Dispatching MemberFunc (Arg=888)...\n");
        auto d2 = MakeDelegate(&handler, &SimpleHandler::MemberFunc, worker);
        d2.AsyncInvoke(888);

        // Test C: Async Lambda (Function Pointer Optimization)
        //
        // Adding '+' forces the lambda to decay to a C-style Function Pointer.
        // This avoids std::function entirely and is extremely lightweight.
        printf("  >> Dispatching Lambda (Arg=999)...\n");
        auto d3 = MakeDelegate(
            +[](int x) { printf("  [Simple] Lambda: %d (Task: %s)\n", x, pcTaskGetName(NULL)); },
            worker);
        d3.AsyncInvoke(999);

        // Wait & Cleanup
        printf("  >> Main Task Waiting for Worker...\n");
        vTaskDelay(pdMS_TO_TICKS(500));

        printf("  >> Stopping Worker...\n");
        worker.ExitThread();
    } else {
        printf("  [FAIL] Could not create Worker Thread!\n");
        BSP_LED_On(LED5);
    }

    printf("\n=== SIMPLE TESTS COMPLETE ===\n");
    BSP_LED_On(LED4);
}
