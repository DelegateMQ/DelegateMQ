/// @file main.cpp
/// @brief The sensor node on Windows or Linux, with desktop stand-ins for the
/// ESP32-S3's peripherals (DesktopHal.h).
///
///   sensor_node [seconds] [pc-address]
///
/// seconds: run time; 0 (the default) runs until Ctrl+C.
/// pc-address: where to publish (default 127.0.0.1). Run sensor_pc there.

#include "SensorNode.h"
#include "DesktopHal.h"
#include "DelegateMQ.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace sensornode;

static std::atomic<bool> s_running{true};
static void OnSignal(int) { s_running = false; }

int main(int argc, char* argv[])
{
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 0;
    NodeConfig config;
    if (argc > 2)
        config.pcAddress = argv[2];

    // Like the ESP32-S3: sampling and control on core 1, the network on core 0
    if (std::thread::hardware_concurrency() >= 2) {
        config.appCoreMask = 1u << 1;
        config.netCoreMask = 1u << 0;
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

#ifdef _WIN32
    dmq::util::NetworkContext winsock;
#endif

    desktop::SineSource signal;
    desktop::DriftingTemperature temperature;
    desktop::ConsoleLed led;
    desktop::TouchSimulator touch;
    Platform platform{ signal, temperature, led, touch };

    SensorNode node(platform);
    node.Start(config);
    printf("Sensor node running%s\n", seconds > 0 ? "" : ". Ctrl+C to stop.");

    // The platform's part of the timing: DelegateMQ timers, IsrDelegate posts
    // and the thread watchdogs (on the ESP32-S3, a FreeRTOS task does this)
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    auto nextWatchdog = std::chrono::steady_clock::now();
    while (s_running && (seconds == 0 || std::chrono::steady_clock::now() < end)) {
        dmq::util::Timer::ProcessTimers();
        dmq::util::IsrDelegates::ProcessAll();
        if (std::chrono::steady_clock::now() >= nextWatchdog) {
            dmq::os::Thread::WatchdogCheckAll();
            nextWatchdog += std::chrono::milliseconds(100);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    node.Stop();

    const NodeCounters c = node.GetCounters();
    printf("Sensor node stopped: %u stats windows, %u temperatures, %u touches published; "
           "%u commands; %u dropped by NetThread\n",
           static_cast<unsigned>(c.statsPublished), static_cast<unsigned>(c.temperaturesPublished),
           static_cast<unsigned>(c.touchesPublished), static_cast<unsigned>(c.commands),
           static_cast<unsigned>(c.netDropped));

    // The node's own checks; the PC client checks the data and the commands
    const bool ok = c.statsPublished > 0 && c.temperaturesPublished > 0 && c.netDropped == 0;
    printf("%s\n", ok ? "NODE CHECKS PASSED" : "NODE CHECKS FAILED");
    return ok ? 0 : 1;
}
