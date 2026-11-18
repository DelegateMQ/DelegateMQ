/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// DelegateMQ on bare metal: no RTOS, no threads (DMQ_THREAD_NONE). The tests
/// here are the same on every target; targets/<name>/ supplies the startup
/// code, linker script, toolchain and the 1 ms timer interrupt (bsp.h).
///
/// Each test checks its own results. The program prints ALL TESTS PASSED and
/// exits with status 0, or lists what failed and exits with status 1. Output
/// and exit go through semihosting, so under QEMU the exit status becomes
/// QEMU's own. See README.md.

#include "DelegateMQ.h"
#include "bsp.h"
#include <cstdio>
#include <chrono>
#include <functional>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// Millisecond counter read by dmq::os::BareMetalClock, incremented by the
// target's timer interrupt (bsp.h). BareMetalClock.h declares it extern "C".
volatile uint64_t g_ticks = 0;

// --------------------------------------------------------------------------
// PORT FUNCTIONS
// --------------------------------------------------------------------------

// C linkage version (called from C files and by the C++ overload below)
extern "C" DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
    printf("FAULT: %s line %u\n", file, static_cast<unsigned int>(line));
    BspExit(2);
}

extern "C" DMQ_NORETURN void WatchdogHandler(const char* threadName)
{
    printf("WATCHDOG EXPIRED: %s\n", threadName);
    BspExit(2);
}

// C++ overload — delegates to the C version above
namespace dmq::util {
    DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
    {
        ::FaultHandler(file, line);
    }
}

// --------------------------------------------------------------------------
// TEST HELPERS
// --------------------------------------------------------------------------
static int g_failures = 0;

static void Check(bool ok, const char* what)
{
    if (!ok) {
        printf("  FAIL: %s\n", what);
        g_failures++;
    }
}

// What each callback saw since the last Reset()
struct Calls {
    int free = 0, member = 0, lambda = 0, timer = 0;
    int lastValue = 0;
    void Reset() { *this = Calls(); }
};
static Calls g_calls;

// --------------------------------------------------------------------------
// CALLBACK FUNCTIONS
// --------------------------------------------------------------------------
void FreeFunction(int val) {
    printf("  [Callback] FreeFunction called! Value: %d\n", val);
    g_calls.free++;
    g_calls.lastValue = val;
}

class TestHandler {
public:
    void MemberFunc(int val) {
        printf("  [Callback] MemberFunc called! Value: %d (Instance: %p)\n", val, static_cast<void*>(this));
        g_calls.member++;
        g_calls.lastValue = val;
    }

    void OnTimerExpired() {
        printf("  [Callback] Timer Expired!\n");
        g_calls.timer++;
    }
};

// --------------------------------------------------------------------------
// MAIN
// --------------------------------------------------------------------------
int main() {
    // Unbuffered output: each printf goes straight to semihosting
    setvbuf(stdout, NULL, _IONBF, 0);

    // 1 ms timer interrupt: drives BareMetalClock and Timer::ProcessTimers()
    BspStartTick();

    printf("\n=========================================\n");
    printf("   BARE METAL DELEGATE SYSTEM ONLINE     \n");
    printf("   %s\n", BspName());
    printf("=========================================\n");

    // --- TEST 1: Unicast Delegate ---
    printf("\n[Test 1] Unicast Delegate (Free Function):\n");
    g_calls.Reset();
    UnicastDelegate<void(int)> unicast;
    unicast = MakeDelegate(FreeFunction);
    unicast(100);
    Check(g_calls.free == 1 && g_calls.lastValue == 100, "free function called once with 100");

    // --- TEST 2: Lambda Support ---
    printf("\n[Test 2] Unicast Delegate (Lambda):\n");
    g_calls.Reset();
    int capture_value = 42;
    int lambdaSum = 0;
    unicast = MakeDelegate(std::function<void(int)>([capture_value, &lambdaSum](int val) {
        printf("  [Callback] Lambda called! Capture: %d, Arg: %d\n", capture_value, val);
        g_calls.lambda++;
        lambdaSum = capture_value + val;
    }));
    unicast(200);
    Check(g_calls.lambda == 1 && lambdaSum == 242, "lambda called once with capture 42 and 200");

    // --- TEST 3: Multicast Delegate ---
    printf("\n[Test 3] Multicast Delegate (Broadcast):\n");
    g_calls.Reset();
    MulticastDelegate<void(int)> multicast;
    TestHandler handler;

    multicast += MakeDelegate(FreeFunction);
    multicast += MakeDelegate(&handler, &TestHandler::MemberFunc);
    multicast += MakeDelegate(std::function<void(int)>([](int val) {
        printf("  [Callback] Multicast Lambda called! Val: %d\n", val);
        g_calls.lambda++;
    }));

    printf("Firing all 3 targets...\n");
    multicast(300);
    Check(g_calls.free == 1 && g_calls.member == 1 && g_calls.lambda == 1,
          "each of the 3 targets called once");

    // --- TEST 4: Removal ---
    printf("\n[Test 4] Removing a Delegate:\n");
    g_calls.Reset();
    multicast -= MakeDelegate(FreeFunction);
    printf("Firing remaining targets (Expected: 2)...\n");
    multicast(400);
    Check(g_calls.free == 0 && g_calls.member == 1 && g_calls.lambda == 1,
          "removed free function not called; the other 2 called once");

    // --- TEST 5: Signals & Connections (RAII) ---
    printf("\n[Test 5] Signals & Scoped Connections:\n");
    g_calls.Reset();
    Signal<void(int)> signal;
    {
        // The connection lasts only for this block
        printf("  -> Creating ScopedConnection inside block...\n");
        ScopedConnection conn = signal.Connect(MakeDelegate(FreeFunction));

        printf("  -> Firing Signal (Expect Callback):\n");
        signal(500);
        Check(g_calls.free == 1 && g_calls.lastValue == 500, "connected signal called the slot");

        printf("  -> Exiting block (ScopedConnection will destruct)...\n");
    }
    // The connection's destructor disconnected it
    printf("  -> Firing Signal outside block (Expect NO Callback):\n");
    signal(600);
    Check(g_calls.free == 1, "disconnected signal did not call the slot");

    // --- TEST 6: Timer (one-shot), driven by the real timer interrupt ---
    printf("\n[Test 6] Timer Delegate (One-Shot):\n");
    g_calls.Reset();
    {
        // ~Timer() unregisters it from the global timer list, so it's a plain local
        Timer myTimer;
        ScopedConnection timerConn = myTimer.OnExpired.Connect(MakeDelegate(&handler, &TestHandler::OnTimerExpired));

        printf("  -> Starting Timer (200ms delay)...\n");
        myTimer.Start(std::chrono::milliseconds(200), true); // true = one-shot, not periodic

        // No sleep on bare metal: busy-wait on the same BareMetalClock the
        // Timer uses. The interrupt handler, not this loop, detects the expiry
        // and fires OnExpired through Timer::ProcessTimers(). If the interrupt
        // never runs, g_ticks never moves and the bounded loop gives up.
        const uint64_t startTicks = g_ticks;
        auto waitStart = BareMetalClock::now();
        for (unsigned long spins = 0; spins < 500000000UL; spins++) {
            if (BareMetalClock::now() - waitStart >= std::chrono::milliseconds(400))
                break;
        }
        myTimer.Stop();
        Check(g_ticks != startTicks, "timer interrupt advanced g_ticks");
        Check(g_calls.timer == 1, "one-shot timer fired exactly once");
    }

    printf("\n=========================================\n");
    if (g_failures == 0)
        printf("           ALL TESTS PASSED              \n");
    else
        printf("           %d CHECK(S) FAILED             \n", g_failures);
    printf("=========================================\n");

    // Semihosting exit: under QEMU this ends QEMU with the same status
    BspExit(g_failures == 0 ? 0 : 1);
}
