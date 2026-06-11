#include "DelegateMQ.h"
#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

#if defined(DMQ_DATABUS)

using namespace dmq;
using namespace dmq::os;
using namespace dmq::databus;

int DataBusSpyTestMain() {
    std::cout << "Starting DataBusSpyTest..." << std::endl;

    DataBus::ResetForTesting();

    // Register stringifiers for topics we want to spy on
    DataBus::RegisterStringifier<int>("sensor/temp", dmq::MakeDelegate([](int val) -> dmq::xstring {
        dmq::xostringstream ss;
        ss << val << " C";
        return ss.str();
    }));
    DataBus::RegisterStringifier<dmq::xstring>("system/status", dmq::MakeDelegate([](const dmq::xstring& val) -> dmq::xstring {
        return val;
    }));

    dmq::xstring lastTopic;
    dmq::xstring lastValue;
    uint64_t lastTimestamp = 0;

    // Connect the spy monitor
    auto spyConn = DataBus::Monitor(dmq::MakeDelegate([&](const SpyPacket& packet) {
        std::cout << "[SPY] " << packet.timestamp_us << " " << packet.topic.c_str() << " = " << packet.value.c_str() << std::endl;
        lastTopic = packet.topic;
        lastValue = packet.value;
        lastTimestamp = packet.timestamp_us;
    }));

    // Publish data
    DataBus::Publish<int>("sensor/temp", 22);
    ASSERT_TRUE(lastTopic == "sensor/temp");
    ASSERT_TRUE(lastValue == "22 C");
    ASSERT_TRUE(lastTimestamp > 0);

    DataBus::Publish<dmq::xstring>("system/status", "OK");
    ASSERT_TRUE(lastTopic == "system/status");
    ASSERT_TRUE(lastValue == "OK");

    // Publish something without a stringifier
    DataBus::Publish<float>("unknown/topic", 1.23f);
    ASSERT_TRUE(lastTopic == "unknown/topic");
    ASSERT_TRUE(lastValue == "?");

    // 2. Async monitor — callback dispatches to the specified worker thread
    {
        DataBus::ResetForTesting();
        Thread monThread("MonitorWorker");
        monThread.CreateThread();

        std::atomic<bool> monitorFired{false};
        std::atomic<bool> calledOnWorker{false};

        auto monConn = DataBus::Monitor(dmq::MakeDelegate([&](const SpyPacket&) {
            calledOnWorker = monThread.IsCurrentThread();
            monitorFired = true;
        }), &monThread);

        DataBus::Publish<int>("async/monitor", 7);

        int retries = 0;
        while (!monitorFired && retries++ < 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        ASSERT_TRUE(monitorFired == true);
        ASSERT_TRUE(calledOnWorker == true);

        monThread.ExitThread();
    }

    std::cout << "DataBusSpyTest PASSED!" << std::endl;
    return 0;
}

#else

int DataBusSpyTestMain() {
    return 0;
}

#endif
