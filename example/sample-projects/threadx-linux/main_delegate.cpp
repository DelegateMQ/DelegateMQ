/// @file main_delegate.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief DelegateMQ feature demo running on ThreadX (Linux/GNU simulation port).
///
/// Mirrors the freertos-bare-metal sample's test suite -- same delegate,
/// Signal, ScopedConnection, Thread, and Timer usage -- to show that the
/// application code is identical across RTOS ports; only DMQ_THREAD changes.

#include "DelegateMQ.h"
#include "tx_api.h"
#include <cstdio>
#include <cstdlib>
#include <functional>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// Defined in DelegateThreadsTests.cpp -- see Test 9 below and the
// file-level comment in DelegateThreadsTests.cpp for what it does and does
// not currently run.
extern void DelegateThreadsTests();

// Defined in TimerDelegateTests.cpp
extern void TimerDelegateTests();

// --------------------------------------------------------------------------
// THREADX CONFIGURATION & HELPERS
// --------------------------------------------------------------------------

// ThreadX's default tick rate is TX_TIMER_TICKS_PER_SECOND (100 Hz == 10ms/tick).
#define TX_MS_TO_TICKS(ms) ((ms) / (1000UL / TX_TIMER_TICKS_PER_SECOND))

// 8192 bytes was enough for the original 8-test demo. Left at 65536 bytes
// (deep call chains, e.g. DelegateThreadsTests(), plus std::mt19937's ~2.5KB
// of internal state per instance in a getRandomTime()-style helper, can
// exceed the original size -- confirmed as the actual cause of a segfault on
// the freertos-linux sibling sample). Ruled out as the cause of the ThreadX
// hang below -- kept anyway since it's cheap insurance and still correct.
#define MAIN_THREAD_STACK_SIZE 65536

static TX_TIMER g_systemTimer;
static TX_THREAD g_mainThread;
static UCHAR g_mainThreadStack[MAIN_THREAD_STACK_SIZE];

static const char* CurrentThreadName()
{
    TX_THREAD* thread = tx_thread_identify();
    return thread ? thread->tx_thread_name : "ISR/Init";
}

static void SystemTimerCallback(ULONG)
{
    // Process all delegate-based timers
    Timer::ProcessTimers();
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
    tx_thread_sleep(TX_MS_TO_TICKS(100));

    printf("\n=========================================\n");
    printf("   THREADX DELEGATE SYSTEM ONLINE        \n");
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

        tx_thread_sleep(TX_MS_TO_TICKS(100));
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
    tx_thread_sleep(TX_MS_TO_TICKS(300));
    myTimer.Stop();

    // --- TEST 9: FullPolicy Stress Tests ---
    // DelegateThreadsTests() only runs ThreadFullPolicyTests() here -- see
    // the file-level comment in DelegateThreadsTests.cpp for why the other
    // four sub-tests are disabled (a ThreadX Linux/GNU port kernel deadlock,
    // not a DelegateMQ bug).
    printf("\n[Test 9] FullPolicy Tests:\n");
    DelegateThreadsTests();

    // --- TEST 10: PacedDispatch & TimerDelegate Tests ---
    printf("\n[Test 10] PacedDispatch & TimerDelegate Tests:\n");
    TimerDelegateTests();

    printf("\n=========================================\n");
    printf("           ALL TESTS PASSED              \n");
    printf("=========================================\n");

    // FUNCTION RETURN:
    // This closing brace '}' forces ~Timer(), ~Thread(), etc. to execute
    // before the ThreadX main thread falls off its entry function.
}

// --------------------------------------------------------------------------
// MAIN THREAD ENTRY
// --------------------------------------------------------------------------
static void MainThreadEntry(ULONG)
{
    ExecuteAllTests();

    // ThreadX has no kernel-level "stop scheduler" call, and this is a
    // simulation running as an ordinary Linux process -- exit() tears down
    // every thread in the process (WorkerThread, the ThreadX timer thread,
    // ...) in one step, which is simpler and more predictable here than
    // trying to individually terminate each ThreadX thread first.
    std::exit(0);
}

// --------------------------------------------------------------------------
// APPLICATION DEFINE (called once by tx_kernel_enter() before scheduling starts)
// --------------------------------------------------------------------------
extern "C" void main_delegate(void* /*first_unused_memory*/)
{
    // Periodic software timer drives DelegateMQ's Timer::ProcessTimers().
    // 1 tick == 10ms at the default TX_TIMER_TICKS_PER_SECOND (100Hz).
    tx_timer_create(&g_systemTimer, const_cast<CHAR*>("SysTimer"), SystemTimerCallback,
                     0, 1, 1, TX_AUTO_ACTIVATE);

    tx_thread_create(&g_mainThread, const_cast<CHAR*>("MainThread"), MainThreadEntry, 0,
                      g_mainThreadStack, MAIN_THREAD_STACK_SIZE,
                      16, 16, TX_NO_TIME_SLICE, TX_AUTO_START);
}
