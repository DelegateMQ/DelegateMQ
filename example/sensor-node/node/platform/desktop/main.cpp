/// @file main.cpp
/// @brief The sensor node on Windows or Linux, with desktop stand-ins for the
/// ESP32-S3's peripherals (DesktopHal.h). The program itself is RunNode().
///
///   sensor_node [seconds] [pc-address]
///
/// seconds: run time; 0 (the default) runs until Ctrl+C.
/// pc-address: where to publish (default 127.0.0.1). Run sensor_pc there.

#include "NodeMain.h"
#include "DesktopHal.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace sensornode;

static std::atomic<bool> s_stop{false};
static void OnSignal(int) { s_stop = true; }

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

    if (seconds == 0)
        printf("Sensor node: Ctrl+C to stop\n");
    const bool ok = RunNode(platform, config, std::chrono::seconds(seconds), &s_stop);
    return ok ? 0 : 1;
}
