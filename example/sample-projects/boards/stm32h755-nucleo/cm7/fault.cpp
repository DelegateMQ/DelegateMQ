/// @file fault.cpp
/// DelegateMQ fault handlers (DMQ_CUSTOM_FAULT_HANDLER, set by CMakeLists.txt),
/// the same as the suite's Cortex-M3 target: print the fault and end the run
/// through BspExit(), which shows it on the LEDs.

#include "DelegateMQ.h"
#include "bsp.h"
#include <cstdio>

extern "C" DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
    printf("\nFAULT: %s line %u\n", file, static_cast<unsigned int>(line));
    BspExit(2);
}

extern "C" {
    void (*BspWatchdogExpected)(const char* threadName) = nullptr;
}

extern "C" DMQ_NORETURN void WatchdogHandler(const char* threadName)
{
    if (BspWatchdogExpected)
        BspWatchdogExpected(threadName);    // a test expects this expiry; it ends the run
    printf("\nWATCHDOG EXPIRED: %s\n", threadName);
    BspExit(2);
}

namespace dmq::util {
    DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
    {
        ::FaultHandler(file, line);
    }
}
