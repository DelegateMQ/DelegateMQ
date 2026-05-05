#include "Fault.h"
#include <cstdlib>

#if defined(_WIN32) || defined(__linux__)
#include <iostream>
#include "delegate/DelegateOpt.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace dmq::util {

//----------------------------------------------------------------------------
// FaultHandler
//----------------------------------------------------------------------------
void FaultHandler(const char* file, unsigned short line)
{
    // @TODO: Update fault handling if necessary.

    // 1. PRINT FIRST (Flush to ensure it appears in CI logs)
    // Excluded on bare-metal/RTOS targets where cout is unavailable.
#if defined(_WIN32) || defined(__linux__)
    std::cout << "FaultHandler called. Application terminated." << std::endl;
    std::cout << "File: " << file << " Line: " << line << std::endl;
    LOG_ERROR("FaultHandler File={} Line={}", file, line);
#endif

    // 2. Break only if interactive or specifically desired
#ifdef _WIN32
    // Optional: Only break if a debugger is actually present to avoid CI crashes
    if (IsDebuggerPresent()) {
        DebugBreak();
    }
#endif

    // 3. Force exit
#if defined(_WIN32) || defined(__linux__)
    abort();    // raises SIGABRT, triggers core dump, returns non-zero exit code
#else
    while(1);   // halt for debugger / watchdog on embedded targets
#endif
}

} // namespace dmq::util

extern "C" void FaultHandler(const char* file, unsigned short line)
{
    dmq::util::FaultHandler(file, line);
}

extern "C" void WatchdogHandler(const char* threadName)
{
#if defined(_WIN32) || defined(__linux__)
    std::cout << "\n************************************************" << std::endl;
    std::cout << "WATCHDOG EXPIRED: " << threadName << std::endl;
    std::cout << "************************************************\n" << std::endl;
    dmq::util::FaultHandler(__FILE__, (unsigned short)__LINE__);
#endif
}
