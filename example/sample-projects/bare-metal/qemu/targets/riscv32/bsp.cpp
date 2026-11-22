/// @file bsp.cpp
/// RV32IMC on QEMU virt: the CLINT machine timer as the 1 ms tick, dispatched
/// through a hand-written RISC-V trap vector (trap_entry.S) since RISC-V,
/// unlike ARM's NVIC, saves nothing automatically on trap entry.

#include "bsp.h"
#include "DelegateMQ.h"
#include <cerrno>
#include <cstdint>

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
// Machine-mode trap dispatch, called (via trap_entry.S) for every trap --
// exception or interrupt alike, since RISC-V has one shared vector in direct
// (mtvec[1:0]=00) mode, unlike ARM's per-exception NVIC slots. Reads mcause
// to identify what happened.
//
// Only the machine timer interrupt (mcause == 0x80000007, i.e. interrupt
// bit set + cause code 7) is handled -- anything else halts.
//
// This is genuinely ISR context: dmq::os::BareMetalCriticalSection's
// mstatus.MIE save/disable/restore (the lock Timer::GetLock() takes) is
// exercised for real here, the RISC-V counterpart of the Arm targets'
// PRIMASK. See CLAUDE.md's "ISR-Safe Locking" section.
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
        dmq::util::Timer::ProcessTimers();
    } else {
        // Unhandled trap (illegal instruction, unexpected exception, ...).
        while (1) { /* halt */ }
    }
}

const char* BspName() { return "RV32IMC, xPack RISC-V GCC, QEMU virt"; }

void BspStartTick() {
    CLINT_MTIMECMP = CLINT_MTIME + TICKS_PER_MS;

    // mtvec direct mode: low 2 bits = 0b00 (trap_entry is 4-byte aligned
    // per trap_entry.S's .align 4, so no masking needed).
    asm volatile("csrw mtvec, %0" :: "r"(reinterpret_cast<uintptr_t>(&trap_entry)));

    // mie.MTIE (bit 7): enable the machine timer interrupt specifically.
    asm volatile("csrs mie, %0" :: "r"(1u << 7));

    // mstatus.MIE (bit 3): enable interrupts globally in machine mode.
    asm volatile("csrs mstatus, %0" :: "r"(1u << 3));
}

// Semihosting SYS_EXIT (0x18) issued directly: the toolchain's libsemihost
// _exit doesn't pass the status on, so QEMU would exit 0 even on failure. On
// RV32, QEMU exits with 0 for the reason code ADP_Stopped_ApplicationExit
// (0x20026) and 1 for any other, here ADP_Stopped_RunTimeErrorUnknown
// (0x20023) -- the same convention as the arm-m4 target's retarget.c.
// The RISC-V semihosting trap is the uncompressed sequence slli/ebreak/srai,
// which must not cross a page boundary (hence the alignment).
void BspExit(int status) {
    register uintptr_t a0 asm("a0") = 0x18;
    register uintptr_t a1 asm("a1") = (status == 0) ? 0x20026u : 0x20023u;
    asm volatile(
        ".option push\n"
        ".option norvc\n"
        ".balign 16\n"
        "slli x0, x0, 0x1f\n"
        "ebreak\n"
        "srai x0, x0, 7\n"
        ".option pop\n"
        : "+r"(a0) : "r"(a1) : "memory");
    while (1) { /* not reached under QEMU with semihosting */ }
}

// newlib's libc.a pulls in _getentropy_r (used to seed things like
// arc4random) as part of its locale/startup machinery even though nothing
// here calls it, leaving an undefined reference at link time. There's no
// hardware RNG under QEMU's direct-boot "virt" machine (no SBI/firmware
// layer to ask), so this stub reports "not available" the same way a real
// target with no TRNG peripheral would.
extern "C" int _getentropy(void*, size_t) {
    errno = ENOSYS;
    return -1;
}
