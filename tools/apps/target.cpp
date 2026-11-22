// TargetApp.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Test application to generate sample DataBus traffic for the Spy tool.
//
// Usage: dmq-target [seconds] [--health-demo]
//   seconds        Run this long, then exit (default: until Ctrl+C)
//   --health-demo  Also cycle dmq-monitor's Health column through OK, WARN and
//                  FAIL: a TransportMonitor whose messages are never ACKed
//                  produces real ACK timeouts, and a simulated RetryMonitor
//                  delivery failure follows.

#include "extras/databus/DataBus.h"
#include "../bridge/spy/SpyBridge.h"
#include "../bridge/node/NodeBridge.h"
#include "extras/util/ThreadMonitor.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <random>
#include <cstring>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <sstream>

int main(int argc, char* argv[]) {
    bool healthDemo = false;
    int duration = 0;   // seconds; 0 = until Ctrl+C
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--health-demo") == 0) healthDemo = true;
        else if (std::isdigit(static_cast<unsigned char>(argv[i][0]))) duration = std::atoi(argv[i]);
    }

    std::cout << "Starting Target Application (Source)..." << std::endl;

    // NodeBridge's heartbeat runs on a dmq::util::Timer, and timers only fire
    // when the application drives them.
    std::atomic<bool> running{ true };
    std::thread timerThread([&running] {
        while (running) {
            dmq::util::Timer::ProcessTimers();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // 1. Start the Spy Bridge (opt-in: streams DataBus messages to dmq-spy)
    SpyBridge::Start("127.0.0.1", 9999);

    // 2. Start the Node Bridge (opt-in: sends heartbeats to dmq-monitor)
    NodeBridge::Start("TargetApp", "127.0.0.1", 9998);

    // 2b. Thread statistics for dmq-thread: ThreadMonitor publishes the stats of
    // registered threads (SpyBridge registers its own) every 2 s, and NodeBridge
    // sends them to port 9998 with its heartbeats.
    dmq::util::ThreadMonitor::Enable();

    // 3. Register stringifiers for topics we want to monitor
    dmq::databus::DataBus::RegisterStringifier<int>("sensor/temp", dmq::MakeDelegate([](int v) -> dmq::xstring {
        std::ostringstream ss;
        ss << v << " C";
        return ss.str().c_str();
    }));
    dmq::databus::DataBus::RegisterStringifier<float>("sensor/humidity", dmq::MakeDelegate([](float v) -> dmq::xstring {
        std::ostringstream ss;
        ss << v << " %";
        return ss.str().c_str();
    }));
    dmq::databus::DataBus::RegisterStringifier<dmq::xstring>("system/status", dmq::MakeDelegate([](const dmq::xstring& v) -> dmq::xstring {
        return v;
    }));

    std::default_random_engine generator;
    std::uniform_int_distribution<int> temp_dist(20, 30);
    std::uniform_real_distribution<float> hum_dist(40.0, 60.0);

    // 4. Health demo: report link health to dmq-monitor (NodeBridge::Watch).
    dmq::util::TransportMonitor transportMonitor(std::chrono::milliseconds(200));
    dmq::util::RetryMonitor retryMonitor;
    if (healthDemo) {
        NodeBridge::Watch(transportMonitor);
        NodeBridge::Watch(retryMonitor);
    }

    std::cout << "Publishing data to 127.0.0.1:9999 (Press Ctrl+C to stop)..." << std::endl;
    if (healthDemo)
        std::cout << "Health demo: OK for 15 s, then ACK timeouts (WARN), then a delivery failure (FAIL), repeating." << std::endl;

    uint16_t seq = 0;
    for (int second = 0; duration == 0 || second < duration; ++second) {
        dmq::databus::DataBus::Publish("sensor/temp", temp_dist(generator));
        dmq::databus::DataBus::Publish("sensor/humidity", hum_dist(generator));
        dmq::databus::DataBus::Publish<dmq::xstring>("system/status", "Running OK");

        if (healthDemo) {
            const int phase = second % 45;
            if (phase >= 15 && phase < 30) {
                // A send nobody ACKs: Process() reports TIMEOUT once the 200 ms expire.
                transportMonitor.Add(seq++, 100);
            } else if (phase == 30) {
                // Simulated: a real RetryMonitor fires this after its retry budget runs out.
                retryMonitor.OnDeliveryFailed(100, seq);
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        transportMonitor.Process();
    }

    dmq::util::ThreadMonitor::Disable();
    SpyBridge::Stop();
    NodeBridge::Stop();
    running = false;
    timerThread.join();
    return 0;
}
