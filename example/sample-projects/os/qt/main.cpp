/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief DelegateMQ feature demo running on Qt threads (DMQ_THREAD_QT).
///
/// Same delegate, Signal, ScopedConnection, Thread, and Timer usage as
/// os/posix and the RTOS simulator samples -- showing the application code
/// is unchanged across every port; only DMQ_THREAD changes. Each
/// dmq::os::Thread is a QThread running its own Qt event loop.
///
/// main() creates a QCoreApplication, as a Qt program would, but never calls
/// exec(): the Qt port needs no event loop on the thread that dispatches.

#include "DelegateMQ.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <atomic>
#include <thread>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// The portable tests every OS port sample runs (test/portable/PortableTests.h)
#include "PortableTests.h"

// Defined in QtThreadExitTests.cpp
extern void QtThreadExitTests();

// --------------------------------------------------------------------------
// CALLBACK FUNCTIONS
// --------------------------------------------------------------------------
void FreeFunction(int val) {
    printf("  [Callback] FreeFunction: %d\n", val);
}

class TestHandler {
public:
    void MemberFunc(int val) {
        printf("  [Callback] MemberFunc: %d (Instance: %p)\n", val, (void*)this);
    }

    void OnTimerExpired() {
        printf("  [Callback] Timer Expired!\n");
    }
};

// --------------------------------------------------------------------------
// TEST LOGIC
// --------------------------------------------------------------------------
static void ExecuteAllTests() {
    printf("\n=========================================\n");
    printf("   QT DELEGATE SYSTEM ONLINE             \n");
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

    // Create a worker thread (Active Object) -- a QThread under DMQ_THREAD_QT.
    // NOTE: C++ Destructor ~Thread() MUST run to close the queue properly!
    Thread workerThread("WorkerThread");
    if (workerThread.CreateThread()) {

        auto asyncDelegate = MakeDelegate(FreeFunction, workerThread);

        printf("  -> Dispatching to Worker Thread (Non-Blocking)...\n");
        asyncDelegate(800);

        dmq::ThisThread::sleep_for(std::chrono::milliseconds(100));
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
    dmq::ThisThread::sleep_for(std::chrono::milliseconds(300));
    myTimer.Stop();

    // --- TEST 9: FullPolicy Stress Tests ---
    printf("\n[Test 9] Cross-Thread Dispatch & FullPolicy Tests:\n");
    PortableTests::DelegateThreadsTests();

    // --- TEST 10: PacedDispatch & TimerDelegate Tests ---
    printf("\n[Test 10] PacedDispatch & TimerDelegate Tests:\n");
    PortableTests::TimerDelegateTests();

    // --- TEST 11: Qt port ExitThread() with a blocked producer ---
    printf("\n[Test 11] Qt ExitThread() With a Blocked Producer:\n");
    QtThreadExitTests();

    printf("\n=========================================\n");
    printf("           ALL TESTS PASSED              \n");
    printf("=========================================\n");
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    QCoreApplication app(argc, argv);

    // Periodic background thread drives dmq::util::Timer::ProcessTimers(),
    // as in os/posix. A QTimer would need the main event loop running,
    // and main() is busy running the tests.
    std::atomic<bool> timerExit{ false };
    std::thread timerThread([&timerExit]() {
        while (!timerExit.load()) {
            Timer::ProcessTimers();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    ExecuteAllTests();

    timerExit.store(true);
    timerThread.join();

    // Unlike the RTOS simulator samples, which loop forever, the process
    // exits normally here.
    return 0;
}
