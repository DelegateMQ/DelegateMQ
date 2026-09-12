/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// Example of a bare-metal RISC-V (RV32IMC) project using DelegateMQ, built
/// with the xPack RISC-V Embedded GCC toolchain and run on QEMU's "virt"
/// machine (-bios none -kernel, direct machine-mode boot, no SBI/OpenSBI).
/// Semihosting output via --specs=semihost.specs (xPack's libsemihost.a
/// provides _write/_sbrk/etc.) -- see README.md for setup instructions.
///
/// Same DelegateMQ feature set as bare-metal-arm, including a Timer test
/// driven by a real hardware interrupt -- here, the CLINT machine timer
/// (mtime/mtimecmp) instead of Cortex-M's SysTick, dispatched through a
/// hand-written RISC-V trap vector (trap_entry.S) since RISC-V, unlike
/// ARM's NVIC, saves nothing automatically on trap entry.

#include "DelegateMQ.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <functional>
#include <memory>         // Required for std::make_shared

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

// Global millisecond counter, incremented by the CLINT timer interrupt.
// Read by dmq::os::BareMetalClock (see BareMetalClock.h) -- same contract
// as bare-metal-arm's g_ticks, just driven by a different peripheral.
// (BareMetalClock.h's own "extern "C"" declaration, pulled in transitively
// via DelegateMQ.h above, already gives this definition C linkage.)
volatile uint64_t g_ticks = 0;

// --------------------------------------------------------------------------
// CLINT (Core Local Interruptor) -- RISC-V's mtime/mtimecmp timer facility.
// QEMU's "virt" machine places it at 0x02000000 with a 10MHz timebase
// (confirmed via `qemu-system-riscv32 -M virt -machine dumpdtb=...` +
// `dtc`: clint@2000000, timebase-frequency = 0x989680 = 10,000,000).
// mtimecmp for hart 0 is at CLINT_BASE+0x4000; the shared mtime counter is
// at CLINT_BASE+0xBFF8 -- the standard SiFive CLINT layout QEMU emulates.
// --------------------------------------------------------------------------
static const uintptr_t CLINT_BASE = 0x02000000u;
#define CLINT_MTIMECMP (*(volatile uint64_t*)(CLINT_BASE + 0x4000))
#define CLINT_MTIME    (*(volatile uint64_t*)(CLINT_BASE + 0xBFF8))
static const uint64_t CLINT_FREQ_HZ = 10000000ULL;
static const uint64_t TICKS_PER_MS = CLINT_FREQ_HZ / 1000; // 10000

// Defined in trap_entry.S: saves caller-saved registers, calls
// trap_dispatch() below, restores registers, mret.
extern "C" void trap_entry(void);

// --------------------------------------------------------------------------
// Real RISC-V machine-mode trap dispatch, called (via trap_entry.S) for
// every trap -- exception or interrupt alike, since RISC-V has one shared
// vector in direct (mtvec[1:0]=00) mode, unlike ARM's per-exception NVIC
// slots. Reads mcause to identify what happened.
//
// Only the machine timer interrupt (mcause == 0x80000007, i.e. interrupt
// bit set + cause code 7) is handled -- anything else halts, mirroring
// bare-metal-arm's Default_Handler for unexpected traps.
//
// This is genuinely ISR context: dmq::os::BareMetalCriticalSection's
// mstatus.MIE save/disable/restore (the lock Timer::GetLock() takes) is
// exercised for real here, not simulated -- the same verification
// bare-metal-arm already established for ARM's PRIMASK equivalent, now
// covering RISC-V's mstatus.MIE too. See CLAUDE.md's "ISR-Safe Locking"
// section.
// --------------------------------------------------------------------------
extern "C" void trap_dispatch(void) {
    uint32_t mcause;
    asm volatile("csrr %0, mcause" : "=r"(mcause));

    if (mcause == 0x80000007u) {
        g_ticks = g_ticks + 1;
        // Rearm for the next 1ms period. Safe to touch mtimecmp without
        // extra locking here: hardware already cleared mstatus.MIE on trap
        // entry (restored by mret from mstatus.MPIE), so nothing else on
        // this single hart can preempt this handler.
        CLINT_MTIMECMP = CLINT_MTIME + TICKS_PER_MS;
        Timer::ProcessTimers();
    } else {
        // Unhandled trap (illegal instruction, unexpected exception, ...).
        while (1) { /* halt */ }
    }
}

static void Timer_Init(void) {
    CLINT_MTIMECMP = CLINT_MTIME + TICKS_PER_MS;

    // mtvec direct mode: low 2 bits = 0b00 (trap_entry is 4-byte aligned
    // per trap_entry.S's .align 4, so no masking needed).
    asm volatile("csrw mtvec, %0" :: "r"(reinterpret_cast<uintptr_t>(&trap_entry)));

    // mie.MTIE (bit 7): enable the machine timer interrupt specifically.
    asm volatile("csrs mie, %0" :: "r"(1u << 7));

    // mstatus.MIE (bit 3): enable interrupts globally in machine mode.
    asm volatile("csrs mstatus, %0" :: "r"(1u << 3));
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

// newlib's libc.a pulls in _getentropy_r (used to seed things like
// arc4random) as part of its locale/startup machinery even though nothing
// in this sample calls it directly, leaving an undefined reference at link
// time. There's no hardware RNG under QEMU's direct-boot "virt" machine (no
// SBI/firmware layer to ask), so this stub reports "not available" the same
// way a real target with no TRNG peripheral would -- callers that actually
// need entropy are expected to fall back to a lower-quality seed on error.
extern "C" int _getentropy(void*, size_t) {
    errno = ENOSYS;
    return -1;
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

    // 2. Start the CLINT timer interrupt (1ms period) that drives
    // BareMetalClock and Timer::ProcessTimers() for the rest of this
    // program's lifetime.
    Timer_Init();

    printf("\n=========================================\n");
    printf("   BARE METAL RISC-V DELEGATE SYSTEM ONLINE\n");
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

    // --- TEST 6: RTOS Timer (One-Shot, driven by the real CLINT ISR) ---
    printf("\n[Test 6] Timer Delegate (One-Shot):\n");

    // NOTE: C++ destructor ~Timer() must run to unregister from the global
    // list -- it's a plain local variable here (no RTOS/heap wrapper needed).
    Timer myTimer;
    ScopedConnection timerConn = myTimer.OnExpired.Connect(MakeDelegate(&handler, &TestHandler::OnTimerExpired));

    printf("  -> Starting Timer (200ms delay)...\n");
    myTimer.Start(std::chrono::milliseconds(200), true); // true = one-shot, not periodic

    // No OS/thread sleep exists on bare metal -- busy-wait using the same
    // BareMetalClock the Timer itself relies on for its own timeouts.
    // trap_dispatch() (a real hardware interrupt, not a simulated one) is
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
