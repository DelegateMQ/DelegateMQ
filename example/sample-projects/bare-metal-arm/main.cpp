/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// Example of a bare-metal ARM project using DelegateMQ with semihosting for output.
/// See README.md for setup instructions.

#include "DelegateMQ.h"
#include <cstdio>
#include <chrono>
#include <functional>
#include <memory>         // Required for std::make_shared

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

// Global millisecond counter
volatile uint64_t g_ticks = 0;

// Cortex-M4 SysTick registers (System Control Space). No CMSIS-Core here,
// matching this project's existing style (see Reset_Handler's raw CPACR
// write) -- these are just memory-mapped registers, same on every Cortex-M.
#define SYST_CSR  (*(volatile uint32_t*)0xE000E010)
#define SYST_RVR  (*(volatile uint32_t*)0xE000E014)
#define SYST_CVR  (*(volatile uint32_t*)0xE000E018)
#define SYST_CSR_ENABLE    (1u << 0)
#define SYST_CSR_TICKINT   (1u << 1)
#define SYST_CSR_CLKSOURCE (1u << 2) // processor clock, not the external reference

// QEMU's mps2-an386 model runs its Cortex-M4 core at 25MHz (same value ARM's
// own MPS2 AN386 application note and Zephyr's mps2_an386 board devicetree
// use for this SoC). Reload = (clock / desired_period_hz) - 1.
static const uint32_t SYSCLK_HZ = 25000000;
static void SysTick_Init(uint32_t periodMs) {
    SYST_RVR = (SYSCLK_HZ / 1000) * periodMs - 1;
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_ENABLE | SYST_CSR_TICKINT | SYST_CSR_CLKSOURCE;
}

// Real Cortex-M4 hardware interrupt (SysTick), wired into startup.c's vector
// table -- not a simulated/software stand-in. Drives 'BareMetalClock' (via
// g_ticks) for DelegateMQ's timeouts, and calls Timer::ProcessTimers()
// directly from ISR context, exactly the usage BareMetalCriticalSection.h's
// own doc comment describes: "Timer::ProcessTimers() is commonly driven
// from a hardware ISR ... its lock genuinely needs interrupt masking." This
// is the first time this port's BareMetalCriticalSection has actually been
// exercised from a real interrupt rather than just reasoned through -- see
// CLAUDE.md's "ISR-Safe Locking" section.
extern "C" void SysTick_Handler(void) {
    g_ticks = g_ticks + 1;
    Timer::ProcessTimers();
}

// --------------------------------------------------------------------------
// PORT FUNCTIONS
// --------------------------------------------------------------------------

// C linkage version (called from C files and by the C++ overload below)
extern "C" DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
    printf("FAULT: %s line %u\r\n", file, static_cast<unsigned int>(line));
    while (1);  // halt
}

extern "C" DMQ_NORETURN void WatchdogHandler(const char* threadName)
{
    printf("WATCHDOG EXPIRED: %s\r\n", threadName);
    while (1);
}

// C++ overload — delegates to the C version above
namespace dmq::util {
    DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
    {
        ::FaultHandler(file, line);
    }
}

// --------------------------------------------------------------------------
// CALLBACK FUNCTIONS
// --------------------------------------------------------------------------
void FreeFunction(int val) {
    printf("  [Callback] FreeFunction called! Value: %d\n", val);
}

class TestHandler {
public:
    void MemberFunc(int val) {
        printf("  [Callback] MemberFunc called! Value: %d (Instance: %p)\n", val, static_cast<void*>(this));
    }

    void OnTimerExpired() {
        printf("  [Callback] Timer Expired!\n");
    }
};

// --------------------------------------------------------------------------
// MAIN
// --------------------------------------------------------------------------
int main() {
    // 1. Critical: Disable buffering to stop malloc() crashes
    setvbuf(stdout, NULL, _IONBF, 0);

    // 2. Start the SysTick interrupt (1ms period) that drives BareMetalClock
    // and Timer::ProcessTimers() for the rest of this program's lifetime.
    SysTick_Init(1);

    printf("\n=========================================\n");
    printf("   BARE METAL DELEGATE SYSTEM ONLINE     \n");
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
    
    // 1. Create a Signal as a plain local variable
    Signal<void(int)> signal;

    {
        // 2. Create a Scoped Connection
        // This connection is only valid inside this block scope {}
        printf("  -> Creating ScopedConnection inside block...\n");
        ScopedConnection conn = signal.Connect(MakeDelegate(FreeFunction));

        // Fire signal - Should call FreeFunction
        printf("  -> Firing Signal (Expect Callback):\n");
        signal(500);

        printf("  -> Exiting block (ScopedConnection will destruct)...\n");
    }

    // 3. Fire again outside scope
    // The connection should have automatically disconnected!
    printf("  -> Firing Signal outside block (Expect NO Callback):\n");
    signal(600);

    // --- TEST 6: RTOS Timer (One-Shot, driven by the real SysTick ISR) ---
    printf("\n[Test 6] Timer Delegate (One-Shot):\n");

    // NOTE: C++ destructor ~Timer() must run to unregister from the global
    // list -- it's a plain local variable here (no RTOS/heap wrapper needed).
    Timer myTimer;
    ScopedConnection timerConn = myTimer.OnExpired.Connect(MakeDelegate(&handler, &TestHandler::OnTimerExpired));

    printf("  -> Starting Timer (200ms delay)...\n");
    myTimer.Start(std::chrono::milliseconds(200), true); // true = one-shot, not periodic

    // No OS/thread sleep exists on bare metal -- busy-wait using the same
    // BareMetalClock the Timer itself relies on for its own timeouts.
    // SysTick_Handler (a real hardware interrupt, not a simulated one) is
    // what actually detects the expiry and fires OnExpired via
    // Timer::ProcessTimers() while this loop spins.
    auto waitStart = BareMetalClock::now();
    while (BareMetalClock::now() - waitStart < std::chrono::milliseconds(400)) {
        // spin
    }
    myTimer.Stop();

    printf("\n=========================================\n");
    printf("           ALL TESTS PASSED              \n");
    printf("=========================================\n");
    printf("To Exit QEMU: Press Ctrl+a, release, then press x.\n");

    while(1);
    return 0;
}