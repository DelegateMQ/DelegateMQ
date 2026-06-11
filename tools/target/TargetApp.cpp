// TargetApp.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Test application to generate sample DataBus traffic for the Spy tool.

#include "extras/databus/DataBus.h"
#include "../bridge/SpyBridge.h"
#include "../bridge/NodeBridge.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <random>

int main() {
    std::cout << "Starting Target Application (Source)..." << std::endl;

    // 1. Start the Spy Bridge (opt-in: streams DataBus messages to dmq-spy)
    SpyBridge::Start("127.0.0.1", 9999);

    // 2. Start the Node Bridge (opt-in: sends heartbeats to dmq-monitor)
    NodeBridge::Start("TargetApp", "127.0.0.1", 9998);

    // 3. Register stringifiers for topics we want to monitor
    dmq::databus::DataBus::RegisterStringifier<int>("sensor/temp", dmq::MakeDelegate([](int v) -> dmq::xstring {
        dmq::xostringstream ss;
        ss << v << " C";
        return ss.str();
    }));
    dmq::databus::DataBus::RegisterStringifier<float>("sensor/humidity", dmq::MakeDelegate([](float v) -> dmq::xstring {
        dmq::xostringstream ss;
        ss << v << " %";
        return ss.str();
    }));
    dmq::databus::DataBus::RegisterStringifier<dmq::xstring>("system/status", dmq::MakeDelegate([](const dmq::xstring& v) -> dmq::xstring {
        return v;
    }));

    std::default_random_engine generator;
    std::uniform_int_distribution<int> temp_dist(20, 30);
    std::uniform_real_distribution<float> hum_dist(40.0, 60.0);

    std::cout << "Publishing data to 127.0.0.1:9999 (Press Ctrl+C to stop)..." << std::endl;

    while (true) {
        dmq::databus::DataBus::Publish("sensor/temp", temp_dist(generator));
        dmq::databus::DataBus::Publish("sensor/humidity", hum_dist(generator));
        dmq::databus::DataBus::Publish<dmq::xstring>("system/status", "Running OK");

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    SpyBridge::Stop();
    NodeBridge::Stop();
    return 0;
}
