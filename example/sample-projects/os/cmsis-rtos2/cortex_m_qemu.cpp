/// @file cortex_m_qemu.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// Built only for a Cortex-M board under QEMU (e.g. mps2/an385), not for
/// native_sim (see CMakeLists.txt). Ends QEMU through semihosting with the
/// test result: 0 when main_delegate.cpp finishes, non-zero on a DelegateMQ
/// fault (DMQ_CUSTOM_FAULT_HANDLER) or a Zephyr fatal error (CPU fault, stack
/// overflow, k_panic), so a failed run ends at once instead of halting in a
/// loop until the test runner's timeout.

#include "DelegateMQ.h"
#include <zephyr/fatal.h>
#include <cstdio>

/// Semihosting SYS_EXIT. QEMU exits with status 0 for reason
/// ADP_Stopped_ApplicationExit (0x20026) and 1 for any other.
extern "C" DMQ_NORETURN void QemuExit(int status)
{
    fflush(stdout);
    register int r0 __asm("r0") = 0x18;
    register int r1 __asm("r1") = (status == 0) ? 0x20026 : 0x20023;
    __asm volatile ("bkpt 0xAB" : : "r" (r0), "r" (r1) : "memory");
    while (true) {}
}

extern "C" DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
    printf("\nFAULT: %s line %u\n", file, static_cast<unsigned int>(line));
    QemuExit(2);
}

extern "C" DMQ_NORETURN void WatchdogHandler(const char* threadName)
{
    printf("\nWATCHDOG EXPIRED: %s\n", threadName);
    QemuExit(2);
}

namespace dmq::util {
    DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
    {
        ::FaultHandler(file, line);
    }
}

/// Zephyr's fatal error hook (weak in the kernel): it has already logged the
/// error and the faulting context.
extern "C" void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf* esf)
{
    (void)esf;
    printf("\nZEPHYR FATAL ERROR: reason %u\n", reason);
    QemuExit(3);
}
