/// @file main.cpp
/// @brief The sensor node on Windows or Linux, with desktop stand-ins for the
/// ESP32-S3's peripherals (DesktopHal.h). The program itself is RunNode().
///
///   sensor_node [seconds] [pc-address] [--tools]
///
/// seconds: run time; 0 (the default) runs until Ctrl+C.
/// pc-address: where to publish (default 127.0.0.1). Run sensor_pc there.
/// --tools: feed DelegateMQ's diagnostic tools (dmq-spy, dmq-monitor,
/// dmq-thread; see DesktopTools.h). run_sensor_node.py starts them all.

#include "NodeMain.h"
#include "DesktopHal.h"
#include "DesktopTools.h"
#include "StdFineClock.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>

using namespace sensornode;

static std::atomic<bool> s_stop{false};
static void OnSignal(int) { s_stop = true; }

int main(int argc, char* argv[])
{
    // Positional arguments, and the --tools flag anywhere
    std::vector<std::string> args;
    bool tools = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--tools") == 0)
            tools = true;
        else
            args.emplace_back(argv[i]);
    }
    const int seconds = args.size() > 0 ? std::atoi(args[0].c_str()) : 0;
    NodeConfig config;
    if (args.size() > 1)
        config.pcAddress = args[1];
    if (tools) {
        config.onStarted = [](SensorNode& node) { desktop::StartTools(node, "SensorNode"); };
        config.onStopping = [](SensorNode& node) { desktop::StopTools(node); };
    }

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
    StdFineClock clock;
    Platform platform{ signal, temperature, led, touch, clock };

    if (seconds == 0)
        printf("Sensor node: Ctrl+C to stop\n");
    const bool ok = RunNode(platform, config, std::chrono::seconds(seconds), &s_stop);
    return ok ? 0 : 1;
}
