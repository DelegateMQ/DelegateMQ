// main.cpp
// DataBus System Architecture Server (Publisher) Example.
//
// This sample demonstrates a distributed publisher using dmq::DataBus over UDP.
// - Publishes simulated sensor/actuator data (DataMsg) to the client.
// - Subscribes to incoming commands (CommandMsg) to dynamically adjust the polling rate.
// - Uses the DelegateMQ Reliability Layer (RetryMonitor/TransportMonitor) to track delivery.
//
// NOTE: To monitor this application with the DelegateMQ Spy Console:
// 1. Build with -DDMQ_DATABUS_TOOLS=ON.
// 2. Start dmq-spy.exe before running this application.

#include "DelegateMQ.h"
#include "SystemMessages.h"
#include "SystemIds.h"
#ifdef DMQ_DATABUS_TOOLS
#include "SpyBridge.h"
#endif

#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <sstream>
#include <atomic>
#include <csignal>

// Global polling rate modified by CommandMsg
std::atomic<int> g_pollingRateMs{ 1000 };
static std::atomic<bool> g_running(true);

static void SignalHandler(int) { g_running = false; }

// Holds all network and DataBus infrastructure for the server.
struct ServerState {
    dmq::transport::UdpTransport transportData;
    dmq::transport::UdpTransport transportCmd;
    dmq::util::TransportMonitor monitor{ std::chrono::seconds(1) };
    std::unique_ptr<dmq::util::RetryMonitor> retry;
    std::unique_ptr<dmq::util::ReliableTransport> reliableTransport;
    std::shared_ptr<dmq::databus::Participant> dataParticipant;
    std::shared_ptr<dmq::databus::Participant> commandParticipant;
    dmq::serialization::serializer::Serializer<void(DataMsg)> dataSerializer;
    dmq::serialization::serializer::Serializer<void(CommandMsg)> commandSerializer;
    dmq::ScopedConnection commandConn;
    dmq::ScopedConnection statusConn;
};

// Create UDP transports: PUB for outgoing data, SUB for incoming commands.
static bool SetupTransports(ServerState& s) {
    if (s.transportData.Create(dmq::transport::UdpTransport::Type::PUB, "127.0.0.1", 8000) != 0) {
        std::cerr << "Failed to create Data transport" << std::endl;
        return false;
    }
    if (s.transportCmd.Create(dmq::transport::UdpTransport::Type::SUB, "127.0.0.1", 8001) != 0) {
        std::cerr << "Failed to create Command transport" << std::endl;
        return false;
    }
    return true;
}

// Wrap the data transport with a reliability layer and cross-wire ACKs.
static void SetupReliability(ServerState& s) {
    s.retry = std::make_unique<dmq::util::RetryMonitor>(s.transportData, s.monitor, 0);
    s.reliableTransport = std::make_unique<dmq::util::ReliableTransport>(s.transportData, *s.retry);

    s.transportData.SetTransportMonitor(&s.monitor);
    s.transportCmd.SetTransportMonitor(&s.monitor);
    s.transportCmd.SetSendTransport(&s.transportData); // Allow SUB to send ACKs via PUB
}

// Register participants and serializers with the DataBus.
static void SetupDataBus(ServerState& s) {
    // Outgoing DataMsg via reliable transport
    s.dataParticipant = std::make_shared<dmq::databus::Participant>(*s.reliableTransport);
    s.dataParticipant->AddRemoteTopic(SystemTopic::DataMsg, SystemTopic::DataMsgId);
    dmq::databus::DataBus::AddParticipant(s.dataParticipant);
    dmq::databus::DataBus::RegisterSerializer<DataMsg>(SystemTopic::DataMsg, s.dataSerializer);

    // Incoming CommandMsg republished to local bus
    s.commandParticipant = std::make_shared<dmq::databus::Participant>(s.transportCmd);
    dmq::databus::DataBus::AddIncomingTopic<CommandMsg>(SystemTopic::CommandMsg, SystemTopic::CommandMsgId, *s.commandParticipant, s.commandSerializer);
}

// Connect local DataBus subscribers and reliability status callbacks.
static void RegisterSubscriptions(ServerState& s) {
    s.commandConn = dmq::databus::DataBus::Subscribe<CommandMsg>(SystemTopic::CommandMsg, [](const CommandMsg& msg) {
        std::cout << "Server received CommandMsg: Changing polling rate to " << msg.pollingRateMs << "ms" << std::endl;
        g_pollingRateMs = msg.pollingRateMs;
    });

    s.statusConn = s.monitor.OnSendStatus.Connect(dmq::MakeDelegate([](dmq::DelegateRemoteId id, uint16_t seq, dmq::util::TransportMonitor::Status status) {
        if (status == dmq::util::TransportMonitor::Status::TIMEOUT) {
            std::cerr << "!!! ALERT: Client not acknowledging data (RemoteID: " << id << " Seq: " << seq << ")" << std::endl;
        }
    }));
}

int main(int argc, char* argv[]) {
    int duration = 0;
    if (argc > 1) duration = atoi(argv[1]);

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "Starting System Architecture SERVER (Publisher)..." << std::endl;

#ifdef _WIN32
    dmq::util::NetworkContext winsock;
#endif

#ifdef DMQ_DATABUS_TOOLS
    SpyBridge::Start("127.0.0.1", 9999);
    dmq::databus::DataBus::RegisterStringifier<DataMsg>(SystemTopic::DataMsg, dmq::MakeDelegate([](const DataMsg& msg) -> dmq::xstring {
        dmq::xostringstream ss;
        ss << "Actuators: " << msg.actuators.size() << ", Sensors: " << msg.sensors.size();
        return ss.str();
    }));
    dmq::databus::DataBus::RegisterStringifier<CommandMsg>(SystemTopic::CommandMsg, dmq::MakeDelegate([](const CommandMsg& msg) -> dmq::xstring {
        dmq::xostringstream ss;
        ss << "Set Polling Rate: " << msg.pollingRateMs << "ms";
        return ss.str();
    }));
#endif

    ServerState s;
    if (!SetupTransports(s)) return -1;
    SetupReliability(s);
    SetupDataBus(s);
    RegisterSubscriptions(s);

    // Background thread: process ACKs, incoming commands, and reliability timeouts
    std::atomic<bool> running{ true };
    std::thread netThread([&]() {
        while (running) {
            s.dataParticipant->ProcessIncoming();
            s.commandParticipant->ProcessIncoming();
            s.monitor.Process();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    if (duration > 0) {
        std::thread([duration]() {
            std::this_thread::sleep_for(std::chrono::seconds(duration));
            g_running = false;
        }).detach();
    } else {
        std::cout << "Press Ctrl+C to quit." << std::endl;
    }

    // Main loop: publish data at the dynamically adjustable rate
    int iteration = 0;
    while (g_running) {
        DataMsg msg;
        Actuator act; act.id = 1; act.position = 10.0f + (float)iteration;
        msg.actuators.push_back(act);

        Sensor sen; sen.id = 101; sen.readingV = 2.5f + ((float)iteration * 0.1f);
        msg.sensors.push_back(sen);

        std::cout << "Publishing DataMsg (Rate: " << g_pollingRateMs << "ms), iteration: " << iteration << std::endl;
        dmq::databus::DataBus::Publish<DataMsg>(SystemTopic::DataMsg, msg);

        std::this_thread::sleep_for(std::chrono::milliseconds(g_pollingRateMs));
        iteration++;
    }

    std::cout << "\nShutting down..." << std::endl;
    running = false;
    netThread.join();
    s.transportData.Close();
    s.transportCmd.Close();

#ifdef DMQ_DATABUS_TOOLS
    SpyBridge::Stop();
#endif

    return 0;
}
