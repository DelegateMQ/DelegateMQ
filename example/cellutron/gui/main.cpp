/**
 * @file gui/main.cpp
 * @brief GUI CPU — Human Machine Interface & Data Logger
 * 
 * This node provides the operator interface for the Cellutron system. 
 * It runs on a standard desktop OS (Windows/Linux) and handles:
 * 1. User commands (Start/Abort) via FTXUI terminal interface.
 * 2. Real-time visualization of instrument telemetry (RPM, Pump Speed).
 * 3. System-wide audit logging to 'logs.txt'.
 * 4. Active alarm monitoring and visualization.
 */

#include "system/System.h"
#include "ui/UI.h"
#include "extras/util/NetworkConnect.h"
#include "DelegateMQ.h"
#include <iostream>

using namespace cellutron;

int main() {
    static dmq::util::NetworkContext networkContext;
    std::cout << "Cellutron GUI Processor starting..." << std::endl;

    cellutron::System::GetInstance().Initialize();

    // Start a background thread to tick the system (heartbeat warmup, etc.)
    // since UI::Start() is a blocking call.
    static dmq::os::Thread tickThread{"TickThread"};
    tickThread.CreateThread();

    dmq::MakeDelegate([]() {
        while (true) {
            cellutron::System::GetInstance().Tick(50);
            dmq::os::Thread::Sleep(std::chrono::milliseconds(50));
        }
    }, tickThread).AsyncInvoke();

    // Start a watchdog thread
    static dmq::os::Thread watchdogThread{"Watchdog", 0, dmq::os::FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "GUI"};
    watchdogThread.CreateThread();

    dmq::MakeDelegate([]() {
        while (true) {
            dmq::os::Thread::WatchdogCheckAll();
            dmq::os::Thread::Sleep(std::chrono::milliseconds(100));
        }
    }, watchdogThread).AsyncInvoke();

    // 4. Start the User Interface (blocks until UI exit)
    cellutron::gui::UI::GetInstance().Start();

    cellutron::System::GetInstance().Shutdown();

    return 0;
}
