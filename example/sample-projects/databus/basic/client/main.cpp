// main.cpp
// DataBus System Architecture Client (Subscriber) Example.
//
// This sample demonstrates a distributed subscriber using dmq::DataBus over UDP.
// - Receives simulated sensor/actuator data (DataMsg) from the server.
// - Periodically sends commands (CommandMsg) to the server to toggle the polling rate.
// - Uses the DelegateMQ Reliability Layer (RetryMonitor/TransportMonitor) to detect connection loss.
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
#include <atomic>
#include <sstream>
#include <csignal>

static std::atomic<bool> g_running(true);

static void SignalHandler(int) { g_running = false; }

// Holds all network and DataBus infrastructure for the client.
struct ClientState {
    dmq::transport::UdpTransport transportData;
    dmq::transport::UdpTransport transportCmd;
    dmq::util::TransportMonitor monitor{ std::chrono::seconds(1) };
    std::unique_ptr<dmq::util::RetryMonitor> retry;
    std::unique_ptr<dmq::util::ReliableTransport> reliableTransport;
    std::shared_ptr<dmq::databus::Participant> dataParticipant;
    std::shared_ptr<dmq::databus::Participant> commandParticipant;
    dmq::serialization::serializer::Serializer<void(DataMsg)> dataSerializer;
    dmq::serialization::serializer::Serializer<void(CommandMsg)> commandSerializer;
    dmq::ScopedConnection dataConn;
    dmq::ScopedConnection statusConn;
};

// Create UDP transports: SUB for incoming data, PUB for outgoing commands.
static bool SetupTransports(ClientState& s) {
    if (s.transportData.Create(dmq::transport::UdpTransport::Type::SUB, "127.0.0.1", 8000) != 0) {
        std::cerr << "Failed to create Data transport" << std::endl;
        return false;
    }
    if (s.transportCmd.Create(dmq::transport::UdpTransport::Type::PUB, "127.0.0.1", 8001) != 0) {
        std::cerr << "Failed to create Command transport" << std::endl;
        return false;
    }
    return true;
}

// Wrap the command transport with a reliability layer and cross-wire ACKs.
static void SetupReliability(ClientState& s) {
    s.retry = std::make_unique<dmq::util::RetryMonitor>(s.transportCmd, s.monitor, 0);
    s.reliableTransport = std::make_unique<dmq::util::ReliableTransport>(s.transportCmd, *s.retry);

    s.transportCmd.SetTransportMonitor(&s.monitor);
    s.transportData.SetTransportMonitor(&s.monitor);
    s.transportData.SetSendTransport(&s.transportCmd); // Allow SUB to send ACKs via PUB
}

// Register participants and serializers with the DataBus.
static void SetupDataBus(ClientState& s) {
    // Outgoing CommandMsg via reliable transport
    s.commandParticipant = std::make_shared<dmq::databus::Participant>(*s.reliableTransport);
    s.commandParticipant->AddRemoteTopic(SystemTopic::CommandMsg, SystemTopic::CommandMsgId);
    dmq::databus::DataBus::AddParticipant(s.commandParticipant);
    dmq::databus::DataBus::RegisterSerializer<CommandMsg>(SystemTopic::CommandMsg, s.commandSerializer);

    // Incoming DataMsg republished to local bus
    s.dataParticipant = std::make_shared<dmq::databus::Participant>(s.transportData);
    dmq::databus::DataBus::AddIncomingTopic<DataMsg>(SystemTopic::DataMsg, SystemTopic::DataMsgId, *s.dataParticipant, s.dataSerializer);
}

// Connect local DataBus subscribers and reliability status callbacks.
static void RegisterSubscriptions(ClientState& s) {
    s.dataConn = dmq::databus::DataBus::Subscribe<DataMsg>(SystemTopic::DataMsg, [](DataMsg msg) {
        std::cout << "Client received DataMsg: " << msg.actuators.size() << " actuators, " << msg.sensors.size() << " sensors" << std::endl;
    });

    s.statusConn = s.monitor.OnSendStatus.Connect(dmq::MakeDelegate([](dmq::DelegateRemoteId id, uint16_t seq, dmq::util::TransportMonitor::Status status) {
        if (status == dmq::util::TransportMonitor::Status::TIMEOUT) {
            std::cerr << "!!! ALERT: Server not responding to command (RemoteID: " << id << " Seq: " << seq << ")" << std::endl;
        } else {
            std::cout << "Command ACK received (Seq: " << seq << ")" << std::endl;
        }
    }));
}

int main(int argc, char* argv[]) {
    int duration = 0;
    if (argc > 1) duration = atoi(argv[1]);

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "Starting System Architecture CLIENT (Subscriber)..." << std::endl;

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
        ss << "Command: PollingRate=" << msg.pollingRateMs << "ms";
        return ss.str();
    }));
#endif

    ClientState s;
    if (!SetupTransports(s)) return -1;
    SetupReliability(s);
    SetupDataBus(s);
    RegisterSubscriptions(s);

    // Background thread: process ACKs, incoming data, and reliability timeouts
    std::atomic<bool> running{ true };
    std::thread receiveThread([&]() {
        while (running) {
            s.commandParticipant->ProcessIncoming();
            s.dataParticipant->ProcessIncoming();
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
        std::cout << "Client running. Sending speed commands every 5s... Press Ctrl+C to quit." << std::endl;
    }

    // Main loop: toggle the server's polling rate every 5 seconds
    int rateToggle = 0;
    auto lastCommandTime = std::chrono::steady_clock::now();

    while (g_running) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCommandTime).count() >= 5) {
            CommandMsg cmd;
            cmd.pollingRateMs = (rateToggle % 2 == 0) ? 500 : 1000;
            std::cout << "Client sending command: set rate to " << cmd.pollingRateMs << "ms" << std::endl;
            dmq::databus::DataBus::Publish<CommandMsg>(SystemTopic::CommandMsg, cmd);

            rateToggle++;
            lastCommandTime = now;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\nShutting down..." << std::endl;
    running = false;
    receiveThread.join();
    s.transportData.Close();
    s.transportCmd.Close();

#ifdef DMQ_DATABUS_TOOLS
    SpyBridge::Stop();
#endif

    return 0;
}
