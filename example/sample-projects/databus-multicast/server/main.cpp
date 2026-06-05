// main.cpp
// DataBus Multicast Server (Publisher) Example.
//
// This sample demonstrates a distributed publisher using dmq::DataBus
// over UDP Multicast for one-to-many "Best Effort" distribution.

#include "DelegateMQ.h"
#include "SystemMessages.h"
#include "SystemIds.h"
#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <csignal>

#ifdef DMQ_DATABUS_TOOLS
#include "SpyBridge.h"
#include <sstream>
#endif

static std::atomic<bool> g_running(true);

static void SignalHandler(int) { g_running = false; }

// Holds all network and DataBus infrastructure for the server.
struct ServerState {
    dmq::transport::MulticastTransport transport;
    std::shared_ptr<dmq::databus::Participant> group;
    dmq::serialization::serializer::Serializer<void(DataMsg)> serializer;
};

// Create the multicast PUB transport.
static bool SetupTransport(ServerState& s, const std::string& localIP) {
    if (s.transport.Create(dmq::transport::MulticastTransport::Type::PUB, "239.1.1.1", 8000, localIP.c_str()) != 0) {
        std::cerr << "Failed to create Multicast transport" << std::endl;
        return false;
    }
    return true;
}

// Register participant and serializer with the DataBus.
static void SetupDataBus(ServerState& s) {
    s.group = std::make_shared<dmq::databus::Participant>(s.transport);
    s.group->AddRemoteTopic(SystemTopic::DataMsg, SystemTopic::DataMsgId);
    dmq::databus::DataBus::AddParticipant(s.group);
    dmq::databus::DataBus::RegisterSerializer<DataMsg>(SystemTopic::DataMsg, s.serializer);
}

int main(int argc, char* argv[]) {
    int duration = 0;
    if (argc > 1) duration = atoi(argv[1]);

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "Starting DataBus Multicast SERVER (Publisher)..." << std::endl;

#ifdef _WIN32
    dmq::util::NetworkContext winsock;
#endif
    std::string localIP = dmq::util::NetworkContext::GetLocalAddress();
    std::cout << "Local Interface: " << localIP << std::endl;

#ifdef DMQ_DATABUS_TOOLS
    SpyBridge::StartMulticast("239.1.1.1", 9999, localIP);
    dmq::databus::DataBus::RegisterStringifier<DataMsg>(SystemTopic::DataMsg, dmq::MakeDelegate([](const DataMsg& msg) -> dmq::xstring {
        dmq::xostringstream ss;
        ss << "Multicast Data: " << msg.actuators.size() << " actuators, " << msg.sensors.size() << " sensors";
        return ss.str();
    }));
#endif

    ServerState s;
    if (!SetupTransport(s, localIP)) return -1;
    SetupDataBus(s);

    if (duration > 0) {
        std::thread([duration]() {
            std::this_thread::sleep_for(std::chrono::seconds(duration));
            g_running = false;
        }).detach();
    } else {
        std::cout << "Press Ctrl+C to quit." << std::endl;
    }

    // Main loop: publish data to the multicast group every second
    int iteration = 0;
    while (g_running) {
        DataMsg msg;
        Actuator act; act.id = 1; act.position = 10.0f + (float)iteration;
        msg.actuators.push_back(act);

        Sensor sen; sen.id = 101; sen.readingV = 2.5f + ((float)iteration * 0.1f);
        msg.sensors.push_back(sen);

        std::cout << "Multicast Publishing DataMsg, iteration: " << iteration << std::endl;
        dmq::databus::DataBus::Publish<DataMsg>(SystemTopic::DataMsg, msg);

        std::this_thread::sleep_for(std::chrono::seconds(1));
        iteration++;
    }

    std::cout << "\nShutting down..." << std::endl;
    s.transport.Close();

#ifdef DMQ_DATABUS_TOOLS
    SpyBridge::Stop();
#endif

    return 0;
}
