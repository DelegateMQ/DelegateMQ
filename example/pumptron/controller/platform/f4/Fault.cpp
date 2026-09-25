/**
 * @file Fault.cpp
 * @brief Pumptron F4 replacement for DelegateMQ's port/fault/Fault.cpp.
 *
 * The library's bare-metal FaultHandler prints and then spins silently, so a
 * board that faults without a debugger attached simply freezes. This version
 * stores a core dump (CoreDump.h) and resets immediately:
 *
 *   - DMQ_ASSERT / BAD_ALLOC   : source file and line
 *   - thread watchdog expired  : name of the thread that stopped responding
 *
 * After the reboot the red LED blinks during startup to show that a crash was
 * detected, and the dump is sent to the GUI.
 *
 * Selected by filtering port/fault/Fault.cpp out of the DelegateMQ sources in
 * platform/f4/CMakeLists.txt (the library's documented override mechanism).
 */

#include "extras/util/Fault.h"
#include "CoreDump.h"

namespace dmq::util {

DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
    CoreDump_StoreAndReset(CORE_DUMP_SRC_ASSERT, file, line, 0);
}

void InstallCrashHandlers() {}

} // namespace dmq::util

extern "C" DMQ_NORETURN void FaultHandler(const char* file, unsigned short line)
{
    dmq::util::FaultHandler(file, line);
}

extern "C" DMQ_NORETURN void WatchdogHandler(const char* threadName)
{
    // The dump's call stack is the watchdog checker's (the Startup task), not
    // the stuck thread's; `where` names the stuck thread.
    CoreDump_StoreAndReset(CORE_DUMP_SRC_WATCHDOG, threadName, 0, 0);
}

extern "C" void InstallCrashHandlers()
{
    dmq::util::InstallCrashHandlers();
}
