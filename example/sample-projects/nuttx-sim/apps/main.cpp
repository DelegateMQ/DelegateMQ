/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief DelegateMQ feature demo running on NuttX's `sim` board (a native
/// host process, no cross-compiler or target hardware -- built the same way
/// `zephyr-linux`/`cmsis-rtos2-linux` verify their respective RTOS ports).
///
/// Identical test suite to zephyr-linux/cmsis-rtos2-linux -- same delegate,
/// Signal, ScopedConnection, Thread, and Timer usage -- to show that the
/// application code is identical across RTOS ports; only DMQ_THREAD changes.
///
/// Unlike Zephyr/CMSIS-RTOS2 (which drive Timer::ProcessTimers() from a
/// genuine timer ISR), this sample drives it from a dedicated high-priority
/// pthread -- NuttXThread.h's own doc comment names both as valid options
/// ("a hardware timer ISR or the highest-priority task in the system"), and
/// a plain pthread avoids pulling in NuttX's signal-based POSIX timer API
/// for this first pass. dmq::os::NuttXCriticalSection (Timer::GetLock()'s
/// ISR-safe lock) is still genuinely exercised by this thread the same way
/// it would be by a real ISR -- Timer's own locking doesn't know or care
/// which kind of caller it has.

#include "DelegateMQ.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <pthread.h>
#include <unistd.h>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// Defined in DelegateThreadsTests.cpp
extern void DelegateThreadsTests();

// Defined in TimerDelegateTests.cpp
extern void TimerDelegateTests();

// --------------------------------------------------------------------------
// SYSTEM TIMER THREAD
// --------------------------------------------------------------------------
static std::atomic<bool> g_timerThreadExit{false};

static void* SystemTimerThread(void*)
{
    while (!g_timerThreadExit.load())
    {
        Timer::ProcessTimers();
        usleep(10000); // 10ms
    }
    return nullptr;
}

static pthread_t StartSystemTimerThread()
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    struct sched_param sp;
    sp.sched_priority = 200; // higher than the default 100 so it preempts reliably
    pthread_attr_setschedparam(&attr, &sp);

    pthread_t tid;
    pthread_create(&tid, &attr, SystemTimerThread, nullptr);
    pthread_attr_destroy(&attr);
    return tid;
}

static const char* CurrentThreadName() {
    // NuttX threads created via dmq::os::Thread are named (pthread_setname_np
    // internally), but there's no portable pthread_getname_np result buffer
    // convention worth depending on here -- print the pthread_t handle
    // itself, which is enough to show cross-thread dispatch is really
    // happening (the callback runs somewhere other than the entry task).
    static thread_local char buf[32];
    snprintf(buf, sizeof(buf), "0x%lx", (unsigned long)pthread_self());
    return buf;
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
    usleep(100000);

    printf("\n=========================================\n");
    printf("   NUTTX DELEGATE SYSTEM ONLINE           \n");
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

    // --- TEST 6: Thread-Safe Multicast (Mutex Protected) ---
    printf("\n[Test 6] Thread-Safe Multicast (Mutex Protected):\n");
    MulticastDelegateSafe<void(int)> safeMulticast;
    safeMulticast += MakeDelegate(FreeFunction);

    printf("Firing Thread-Safe Delegate...\n");
    safeMulticast(700);

    // --- TEST 7: Async Delegate (Cross-Thread Dispatch) ---
    printf("\n[Test 7] Async Delegate (Cross-Thread Dispatch):\n");

    // NOTE: C++ Destructor ~Thread() MUST run to close the queue properly!
    Thread workerThread("WorkerThread");
    if (workerThread.CreateThread()) {

        auto asyncDelegate = MakeDelegate(FreeFunction, workerThread);

        printf("  -> Dispatching to Worker Thread (Non-Blocking)...\n");
        asyncDelegate(800);

        usleep(100000);
    }
    else {
        printf("  [Error] Failed to create WorkerThread!\n");
    }

    // --- TEST 8: Timer Delegate (One-Shot) ---
    printf("\n[Test 8] Timer Delegate (One-Shot):\n");

    // NOTE: C++ Destructor ~Timer() MUST run to unregister from the global list!
    Timer myTimer;

    ScopedConnection timerConn = myTimer.OnExpired.Connect(MakeDelegate(&handler, &TestHandler::OnTimerExpired));

    printf("  -> Starting Timer (200ms delay)...\n");
    myTimer.Start(std::chrono::milliseconds(200), true); // true = one-shot, not periodic

    // Wait for timer
    usleep(300000);
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
extern "C" int dmq_test_main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    pthread_t timerThread = StartSystemTimerThread();

    ExecuteAllTests();

    g_timerThreadExit.store(true);
    pthread_join(timerThread, nullptr);

    // Matches zephyr-linux/cmsis-rtos2-linux/freertos-linux/threadx-linux:
    // exit() tears down every thread in the process in one step, avoiding
    // any board-specific edge case in returning normally from the init task.
    exit(0);
}
