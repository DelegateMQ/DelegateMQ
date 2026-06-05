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

static void OnUnhandledTopic(const dmq::xstring& topic) {
    std::cout << "GUI WARNING: Unhandled topic: " << topic.c_str() << std::endl;
}

static void OnTechnicalError(const dmq::xstring& topic, dmq::DelegateError error) {
    std::cerr << "GUI ERROR: Technical failure on topic: " << topic.c_str() << ", Error: " << (int)error << std::endl;
}

static void OnTick() {
    while (true) {
        cellutron::System::GetInstance().Tick(50);
        dmq::os::Thread::Sleep(std::chrono::milliseconds(50));
    }
}

static void OnWatchdog() {
    while (true) {
        dmq::os::Thread::WatchdogCheckAll();
        dmq::os::Thread::Sleep(std::chrono::milliseconds(100));
    }
}

int main() {
    static dmq::util::NetworkContext networkContext;
    std::cout << "Cellutron GUI Processor starting..." << std::endl;

    cellutron::System::GetInstance().Initialize();

    // Catch unhandled topics (sent but no subscribers)
    static auto unhandledConn = dmq::databus::DataBus::SubscribeUnhandled(dmq::MakeDelegate(&OnUnhandledTopic));

    // Catch technical errors (e.g. serialization failures)
    static auto errorConn = dmq::databus::DataBus::SubscribeError(dmq::MakeDelegate(&OnTechnicalError));

    // Start a background thread to tick the system (heartbeat warmup, etc.)
    // since UI::Start() is a blocking call.
    static dmq::os::Thread tickThread{"TickThread"};
    tickThread.CreateThread();

    dmq::MakeDelegate(&OnTick, tickThread).AsyncInvoke();

    // Start a watchdog thread
    static dmq::os::Thread watchdogThread{"Watchdog", 0, dmq::os::FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "GUI"};
    watchdogThread.CreateThread();

    dmq::MakeDelegate(&OnWatchdog, watchdogThread).AsyncInvoke();

    // 4. Start the User Interface (blocks until UI exit)
    cellutron::gui::UI::GetInstance().Start();

    cellutron::System::GetInstance().Shutdown();

    return 0;
}
