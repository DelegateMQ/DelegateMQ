/**
 * @file gui/main.cpp
 * @brief Pumptron operator console (Windows/Linux, FTXUI).
 *
 * Usage:
 *   pumptron_gui --serial <port> [--baud <rate>]   Real STM32F4 Discovery over RS-232
 *   pumptron_gui --udp                             FreeRTOS simulator on localhost
 *   Add --selftest to run a headless end-to-end check instead of the UI.
 */

#include "DelegateMQ.h"
#include "extras/util/NetworkConnect.h"
#include "system/System.h"
#include "ui/UI.h"
#include "selftest/SelfTest.h"
#include "util/Constants.h"
#if defined(PUMPTRON_HAVE_SERIAL)
#include "libserialport.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace pumptron;

static void PrintUsage(const char* exe)
{
    printf("Pumptron operator console\n\n");
    printf("Usage:\n");
    printf("  %s --serial <port> [--baud <rate>]   STM32F4 Discovery over RS-232 (default %d baud)\n", exe, SERIAL_BAUD);
    printf("  %s --udp                             FreeRTOS simulator (pumptron_controller) on localhost\n", exe);
    printf("  Add --selftest to run a headless end-to-end check of the controller instead of the UI.\n\n");

#if defined(PUMPTRON_HAVE_SERIAL)
    printf("Serial ports found:\n");
    sp_port** ports = nullptr;
    if (sp_list_ports(&ports) == SP_OK && ports && ports[0]) {
        for (int i = 0; ports[i]; ++i) {
            const char* desc = sp_get_port_description(ports[i]);
            printf("  %-12s %s\n", sp_get_port_name(ports[i]), desc ? desc : "");
        }
    } else {
        printf("  (none)\n");
    }
    if (ports) sp_free_port_list(ports);
#else
    printf("(Built without libserialport: --serial is unavailable.)\n");
#endif
}

static bool ParseArgs(int argc, char* argv[], System::Options& options, bool& selfTest)
{
    bool haveLink = false;
    options.baud = SERIAL_BAUD;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--serial") && i + 1 < argc) {
            options.link = System::LinkType::SERIAL;
            options.serialPort = argv[++i];
            haveLink = true;
        } else if (!strcmp(argv[i], "--baud") && i + 1 < argc) {
            options.baud = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--selftest")) {
            selfTest = true;
        } else if (!strcmp(argv[i], "--udp")) {
            options.link = System::LinkType::UDP;
            haveLink = true;
        } else {
            return false;
        }
    }
    return haveLink;
}

static void OnWatchdog()
{
    // Runs until process exit; checks every watchdog-enabled dmq thread.
    for (;;) {
        dmq::os::Thread::WatchdogCheckAll();
        dmq::os::Thread::Sleep(std::chrono::milliseconds(500));
    }
}

int main(int argc, char* argv[])
{
    System::Options options;
    bool selfTest = false;
    if (!ParseArgs(argc, argv, options, selfTest)) {
        PrintUsage(argv[0]);
        return 1;
    }

    ::InstallCrashHandlers();
    static dmq::util::NetworkContext networkContext;

    std::string error;
    if (!System::GetInstance().Initialize(options, error)) {
        fprintf(stderr, "Pumptron GUI: %s\n", error.c_str());
        System::GetInstance().Shutdown();
        return 1;
    }

    static dmq::os::Thread watchdogThread{ "GUI_Watchdog" };
    watchdogThread.CreateThread();
    (void)dmq::MakeDelegate(&OnWatchdog, watchdogThread).AsyncInvoke();

    int result = 0;
    if (selfTest) {
        result = gui::RunSelfTest();
    } else {
        // Blocks until the operator quits.
        gui::UI::GetInstance().Run(System::GetInstance().GetLinkDescription());
    }

    System::GetInstance().Shutdown();

    // The watchdog thread never returns from OnWatchdog(); skip static
    // destructors that would try to join it.
    std::_Exit(result);
}
