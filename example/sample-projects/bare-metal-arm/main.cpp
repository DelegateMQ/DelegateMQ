/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// Example of a bare-metal ARM project using DelegateMQ with semihosting for output.
/// See README.md for setup instructions.

#include "DelegateMQ.h"
#include <cstdio>
#include <functional>
#include <memory>         // Required for std::make_shared

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

// Global millisecond counter
volatile uint64_t g_ticks = 0;  

// @TODO: Cortex-M SysTick Handler (Called every 1ms)
// This drives the 'BareMetalClock' used by DelegateMQ for timeouts.
extern "C" void SysTick_Handler(void) {
    g_ticks = g_ticks + 1;
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
};

// --------------------------------------------------------------------------
// MAIN
// --------------------------------------------------------------------------
int main() {
    // 1. Critical: Disable buffering to stop malloc() crashes
    setvbuf(stdout, NULL, _IONBF, 0);

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

    printf("\n=========================================\n");
    printf("           ALL TESTS PASSED              \n");
    printf("=========================================\n");
    printf("To Exit QEMU: Press Ctrl+a, release, then press x.\n");

    while(1);
    return 0;
}