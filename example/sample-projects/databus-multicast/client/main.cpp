// main.cpp
// DataBus Multicast Client (Subscriber) Example.
//
// This sample demonstrates a distributed subscriber using dmq::DataBus
// over UDP Multicast. Any number of clients can join the group to
// receive the same data stream.

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

// Holds all network and DataBus infrastructure for the client.
struct ClientState {
    dmq::transport::MulticastTransport transport;
    std::shared_ptr<dmq::databus::Participant> group;
    dmq::serialization::serializer::Serializer<void(DataMsg)> serializer;
    dmq::ScopedConnection dataConn;
};

// Create the multicast SUB transport.
static bool SetupTransport(ClientState& s, const std::string& localIP) {
    if (s.transport.Create(dmq::transport::MulticastTransport::Type::SUB, "239.1.1.1", 8000, localIP.c_str()) != 0) {
        std::cerr << "Failed to create Multicast transport" << std::endl;
        return false;
    }
    return true;
}

// Register participant and incoming topic with the DataBus.
static void SetupDataBus(ClientState& s) {
    s.group = std::make_shared<dmq::databus::Participant>(s.transport);
    dmq::databus::DataBus::AddIncomingTopic<DataMsg>(SystemTopic::DataMsg, SystemTopic::DataMsgId, *s.group, s.serializer);
}

// Connect local DataBus subscribers.
static void RegisterSubscriptions(ClientState& s) {
    s.dataConn = dmq::databus::DataBus::Subscribe<DataMsg>(SystemTopic::DataMsg, [](DataMsg msg) {
        std::cout << "Received Multicast Data: " << msg.actuators.size() << " actuators, "
                  << msg.sensors.size() << " sensors" << std::endl;
    });
}

int main(int argc, char* argv[]) {
    int duration = 0;
    if (argc > 1) duration = atoi(argv[1]);

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "Starting DataBus Multicast CLIENT (Subscriber)..." << std::endl;

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

    ClientState s;
    if (!SetupTransport(s, localIP)) return -1;
    SetupDataBus(s);
    RegisterSubscriptions(s);

    // Background thread: process incoming multicast data
    std::atomic<bool> running{ true };
    std::thread receiveThread([&]() {
        while (running) {
            s.group->ProcessIncoming();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    if (duration > 0) {
        std::thread([duration]() {
            std::this_thread::sleep_for(std::chrono::seconds(duration));
            g_running = false;
        }).detach();
    } else {
        std::cout << "Client joined multicast group 239.1.1.1:8000. Waiting for data... Press Ctrl+C to quit." << std::endl;
    }

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\nShutting down..." << std::endl;
    running = false;
    receiveThread.join();
    s.transport.Close();

#ifdef DMQ_DATABUS_TOOLS
    SpyBridge::Stop();
#endif

    return 0;
}
