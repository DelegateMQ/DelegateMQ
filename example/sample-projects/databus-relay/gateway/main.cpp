// main.cpp
// DataBus Relay Example — GATEWAY node (B).
//
//   sensor (A) <--- UDP ---> gateway (B) <--- named pipe ---> monitor (C)
//
// The gateway sits on two different links and bridges DataBus topics between
// them. DelegateMQ never forwards received data on its own: AddIncomingTopic
// delivers to local subscribers only. A relay node opts in per topic with
// AddRelayTopic, which re-publishes incoming data to the local bus AND to every
// participant that lists the topic as outgoing (AddRemoteTopic).
//
// Each relayed topic is wired one way only, so nothing loops back:
//   sensor/temp  : inbound on the UDP participant  -> outgoing on the pipe participant
//   cmd/setpoint : inbound on the pipe participant -> outgoing on the UDP participant
//
// Two transports in one process: CMake selects DMQ_TRANSPORT_WIN32_UDP, and the
// pipe transport header is included directly below. Desktop transports are
// header-only, so this needs no library or build-option change.

#include "DelegateMQ.h"
#include "port/transport/win32-pipe/Win32PipeTransport.h"
#include "SystemMessages.h"
#include "SystemIds.h"

#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstdlib>

using namespace dmq::databus;
using dmq::transport::UdpTransport;
using dmq::transport::Win32PipeTransport;

static std::atomic<bool> g_running(true);

static void SignalHandler(int) { g_running = false; }

struct GatewayState {
    // Ethernet side (sensor)
    UdpTransport udpIn;     // SUB <- sensor (sensor/temp)
    UdpTransport udpOut;    // PUB -> sensor (cmd/setpoint)
    std::shared_ptr<Participant> udpInParticipant;
    std::shared_ptr<Participant> udpOutParticipant;

    // Pipe side (monitor): one duplex pipe, one participant, both directions
    Win32PipeTransport pipe;
    std::shared_ptr<Participant> pipeParticipant;

    dmq::serialization::serializer::Serializer<void(TempMsg)> tempSerializer;
    dmq::serialization::serializer::Serializer<void(SetpointMsg)> setpointSerializer;

    dmq::ScopedConnection tempConn;
    dmq::ScopedConnection setpointConn;
    dmq::ScopedConnection errorConn;
};

static bool SetupTransports(GatewayState& s) {
    if (s.udpIn.Create(UdpTransport::Type::SUB, SystemLink::UdpAddr, SystemLink::GatewayUdpPort) != 0) {
        std::cerr << "[gateway] Failed to create UDP SUB transport" << std::endl;
        return false;
    }
    s.udpIn.SetRecvTimeout(std::chrono::milliseconds(100));

    if (s.udpOut.Create(UdpTransport::Type::PUB, SystemLink::UdpAddr, SystemLink::SensorUdpPort) != 0) {
        std::cerr << "[gateway] Failed to create UDP PUB transport" << std::endl;
        return false;
    }

    // The gateway is the pipe server: it creates the pipe the monitor connects to.
    if (s.pipe.Create(Win32PipeTransport::Type::SUB, SystemLink::PipeName) != 0) {
        std::cerr << "[gateway] Failed to create named pipe " << SystemLink::PipeName << std::endl;
        return false;
    }
    return true;
}

static void SetupDataBus(GatewayState& s) {
    // Serializers for both topics (required for any remote send)
    DataBus::RegisterSerializer<TempMsg>(SystemTopic::SensorTemp, s.tempSerializer);
    DataBus::RegisterSerializer<SetpointMsg>(SystemTopic::CmdSetpoint, s.setpointSerializer);

    // --- sensor/temp: UDP in -> pipe out ---
    s.udpInParticipant = std::make_shared<Participant>(s.udpIn);
    DataBus::AddRelayTopic<TempMsg>(SystemTopic::SensorTemp, SystemTopic::SensorTempId,
        *s.udpInParticipant, s.tempSerializer);

    s.pipeParticipant = std::make_shared<Participant>(s.pipe);
    s.pipeParticipant->AddRemoteTopic(SystemTopic::SensorTemp, SystemTopic::SensorTempId);
    DataBus::AddParticipant(s.pipeParticipant);

    // --- cmd/setpoint: pipe in -> UDP out ---
    // The pipe participant is registered with AddParticipant (it sends sensor/temp),
    // but AddRelayTopic does not make it outgoing for cmd/setpoint, so the command
    // is not echoed back to the monitor.
    DataBus::AddRelayTopic<SetpointMsg>(SystemTopic::CmdSetpoint, SystemTopic::CmdSetpointId,
        *s.pipeParticipant, s.setpointSerializer);

    s.udpOutParticipant = std::make_shared<Participant>(s.udpOut);
    s.udpOutParticipant->AddRemoteTopic(SystemTopic::CmdSetpoint, SystemTopic::CmdSetpointId);
    DataBus::AddParticipant(s.udpOutParticipant);
}

static void RegisterSubscriptions(GatewayState& s) {
    // A relay node is still a normal DataBus node: relayed data also reaches local subscribers.
    s.tempConn = DataBus::Subscribe<TempMsg>(SystemTopic::SensorTemp, [](const TempMsg& msg) {
        std::cout << "[gateway] relayed sensor/temp: " << msg.celsius << " C (seq " << msg.seq
                  << ")  UDP -> pipe" << std::endl;
    });
    s.setpointConn = DataBus::Subscribe<SetpointMsg>(SystemTopic::CmdSetpoint, [](const SetpointMsg& msg) {
        std::cout << "[gateway] relayed cmd/setpoint: " << msg.celsius << " C  pipe -> UDP" << std::endl;
    });

    // Until the monitor connects, pipe sends fail. Report once instead of every message.
    s.errorConn = DataBus::SubscribeError([](const dmq::xstring& topic, dmq::DelegateError err) {
        static std::atomic<bool> reported{ false };
        if (!reported.exchange(true)) {
            std::cout << "[gateway] send error on " << topic << " (error " << static_cast<int>(err)
                      << "); monitor not connected yet? Further errors not shown." << std::endl;
        }
    });
}

int main(int argc, char* argv[]) {
    int duration = 0;
    if (argc > 1) duration = atoi(argv[1]);

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "Starting DataBus Relay GATEWAY (B, UDP <-> pipe)..." << std::endl;

    dmq::util::NetworkContext winsock;

    GatewayState s;
    if (!SetupTransports(s)) return -1;
    SetupDataBus(s);
    RegisterSubscriptions(s);

    // One receive thread per inbound link. Relayed publishes run on the receive
    // thread of the link the data arrived on.
    std::atomic<bool> running{ true };
    std::thread udpThread([&]() {
        while (running)
            s.udpInParticipant->ProcessIncoming();  // blocks up to the 100 ms UDP timeout
    });
    std::thread pipeThread([&]() {
        while (running) {
            while (s.pipeParticipant->ProcessIncoming() == 0) {}  // drain; pipe reads don't block
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    if (duration > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(duration));
        g_running = false;
    } else {
        std::cout << "Press Ctrl+C to quit." << std::endl;
        while (g_running)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\n[gateway] Shutting down..." << std::endl;
    running = false;
    udpThread.join();
    pipeThread.join();
    s.udpIn.Close();
    s.udpOut.Close();
    s.pipe.Close();
    return 0;
}
