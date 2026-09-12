/// @file main_delegate.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief DelegateMQ feature demo running on CMSIS-RTOS2 -- specifically
/// Zephyr's own CMSIS-RTOS v2 compatibility layer (CONFIG_CMSIS_RTOS_V2,
/// subsys/portability/cmsis_rtos_v2/), built for the native_sim simulation
/// board. This is a real, non-stub CMSIS-RTOS2 implementation (Zephyr's own
/// samples/tests run it on native_sim in CI), so this sample exercises
/// dmq::os::CmsisRtos2Thread/Mutex/Semaphore/DelegateQueue against genuine
/// osThreadNew/osMutexAcquire/osSemaphoreAcquire/osMessageQueuePut calls --
/// not the ZephyrThread port (see zephyr-linux for that).
///
/// Identical test suite to zephyr-linux/freertos-linux/threadx-linux -- same
/// delegate, Signal, ScopedConnection, Thread, and Timer usage -- to show
/// that the application code is identical across RTOS ports; only DMQ_THREAD
/// changes. Uses only CMSIS-RTOS2 API calls (osDelay, osTimerNew/osTimerStart,
/// osThreadGetName/osThreadGetId) rather than Zephyr's native kernel API, to
/// keep this genuinely a CMSIS-RTOS2 application rather than a Zephyr one
/// that merely links the compatibility layer in.
///
/// NOTE: this build still goes through `west build` / Zephyr's own main()
/// entry, since CONFIG_CMSIS_RTOS_V2 is a Zephyr kernel subsystem layered on
/// top of the Zephyr scheduler -- there is no truly OS-agnostic way to boot
/// a CMSIS-RTOS2 application outside of some concrete RTOS.

#include "DelegateMQ.h"
#include "cmsis_os2.h"
#include <cstdio>
#include <cstdlib>
#include <functional>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// Defined in DelegateThreadsTests.cpp
extern void DelegateThreadsTests();

// Defined in TimerDelegateTests.cpp
extern void TimerDelegateTests();

// --------------------------------------------------------------------------
// CMSIS-RTOS2 CONFIGURATION & HELPERS
// --------------------------------------------------------------------------
static void SystemTimerHandler(void* /*argument*/) {
    // Process all delegate-based timers
    Timer::ProcessTimers();
}

static const char* CurrentThreadName() {
    const char* name = osThreadGetName(osThreadGetId());
    return (name && name[0]) ? name : "unnamed";
}

// --------------------------------------------------------------------------
// CALLBACK FUNCTIONS
// --------------------------------------------------------------------------
void FreeFunction(int val) {
    printf("  [Callback] FreeFunction: %d (Thread: %s)\n", val, CurrentThreadName());
}

class TestHandler {
public:
    void MemberFunc(int val) {
        printf("  [Callback] MemberFunc: %d (Thread: %s, Instance: %p)\n", val, CurrentThreadName(), (void*)this);
    }

    void OnTimerExpired() {
        printf("  [Callback] Timer Expired! (Thread: %s)\n", CurrentThreadName());
    }
};

// --------------------------------------------------------------------------
// TEST LOGIC
// --------------------------------------------------------------------------
void ExecuteAllTests() {
    setvbuf(stdout, NULL, _IONBF, 0);
    osDelay(100);

    printf("\n=========================================\n");
    printf("   CMSIS-RTOS2 DELEGATE SYSTEM ONLINE     \n");
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

        osDelay(100);
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
    osDelay(300);
    myTimer.Stop();

    // --- TEST 9: Cross-Thread Dispatch & FullPolicy Stress Tests ---
    printf("\n[Test 9] Cross-Thread Dispatch & FullPolicy Tests:\n");
    DelegateThreadsTests();

    // --- TEST 10: PacedDispatch & TimerDelegate Tests ---
    printf("\n[Test 10] PacedDispatch & TimerDelegate Tests:\n");
    TimerDelegateTests();

    printf("\n=========================================\n");
    printf("           ALL TESTS PASSED              \n");
    printf("=========================================\n");

    // FUNCTION RETURN:
    // This closing brace '}' forces ~Timer(), ~Thread(), etc. to execute.
}

// --------------------------------------------------------------------------
// MAIN ENTRY POINT
// --------------------------------------------------------------------------
int main() {
    // osKernelInitialize()/osKernelStart() are unnecessary here: Zephyr's own
    // main() already runs with the kernel/scheduler up (its CMSIS-RTOS2
    // compat layer initializes at SYS_INIT time, before app main() is
    // called), unlike a bare CMSIS-RTOS2 RTX5 target where main() itself
    // must call osKernelInitialize()/osKernelStart() to boot the RTOS.

    // Periodic software timer drives DelegateMQ's Timer::ProcessTimers(),
    // via the genuine CMSIS-RTOS2 osTimer API (backed by Zephyr's k_timer
    // under the compatibility layer).
    osTimerId_t systemTimer = osTimerNew(SystemTimerHandler, osTimerPeriodic, nullptr, nullptr);
    osTimerStart(systemTimer, 10); // 10ms period

    ExecuteAllTests();

    // native_sim runs as an ordinary Linux process -- exit() tears down
    // every thread in the process (WorkerThread, everything) in one step,
    // the same approach zephyr-linux/freertos-linux/threadx-linux use.
    // Returning from main() normally hits a native_sim/posix-arch
    // thread-table teardown edge case instead.
    exit(0);
}
