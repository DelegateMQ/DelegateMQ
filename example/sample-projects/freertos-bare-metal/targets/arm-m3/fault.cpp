/// @file fault.cpp
/// DelegateMQ fault handlers for the QEMU run (DMQ_CUSTOM_FAULT_HANDLER, set
/// by target.cmake). The library's own embedded handler halts in a loop, as
/// firmware should; under QEMU a failed check must end the run instead, so
/// these print and exit through semihosting with a non-zero status.

#include "DelegateMQ.h"
#include "bsp.h"
#include <cstdio>

extern "C" DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
    printf("\nFAULT: %s line %u\n", file, static_cast<unsigned int>(line));
    BspExit(2);
}

extern "C" DMQ_NORETURN void WatchdogHandler(const char* threadName)
{
    printf("\nWATCHDOG EXPIRED: %s\n", threadName);
    BspExit(2);
}

namespace dmq::util {
    DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
    {
        ::FaultHandler(file, line);
    }
}
