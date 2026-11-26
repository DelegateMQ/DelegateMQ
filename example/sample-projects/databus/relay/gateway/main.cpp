// main.cpp
// DataBus Relay Example — GATEWAY node (B).
//
//   sensor (A) <--- UDP ---> gateway (B) <--- local IPC ---> monitor (C)
//
// Local IPC is a Win32 named pipe on Windows and a Unix domain datagram socket
// on Linux, or shared memory with --shm (ShmLink.h). All are duplex: one
// transport, one participant, both directions.
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
// Two transports in one process: CMake selects the platform's UDP transport, and
// the local IPC transport header is included directly below. Desktop transports
// are header-only, so this needs no library or build-option change.

#include "DelegateMQ.h"
#ifdef _WIN32
#include "port/transport/win32-pipe/Win32PipeTransport.h"
#else
#include "port/transport/linux-uds/LinuxUdsTransport.h"
#endif
#include "SystemMessages.h"
#include "SystemIds.h"
#include "ShmLink.h"

#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstdlib>

using namespace dmq::databus;
using dmq::transport::UdpTransport;
#ifdef _WIN32
using IpcTransport = dmq::transport::Win32PipeTransport;
#else
using IpcTransport = dmq::transport::LinuxUdsTransport;
#endif

static std::atomic<bool> g_running(true);
static const char* g_ipcName = SystemLink::IpcName;    // "shm" with --shm

static void SignalHandler(int) { g_running = false; }

struct GatewayState {
    // Ethernet side (sensor)
    UdpTransport udpIn;     // SUB <- sensor (sensor/temp)
    UdpTransport udpOut;    // PUB -> sensor (cmd/setpoint)
    std::shared_ptr<Participant> udpInParticipant;
    std::shared_ptr<Participant> udpOutParticipant;

    // Pipe side (monitor): one duplex IPC link, one participant, both directions.
    // `ipc` is the pipe/socket, or the shared memory with --shm.
    IpcTransport pipe;
    ShmTransport shm;
    dmq::transport::ITransport* ipc = &pipe;
    std::shared_ptr<Participant> pipeParticipant;

    dmq::serialization::serializer::Serializer<void(TempMsg)> tempSerializer;
    dmq::serialization::serializer::Serializer<void(SetpointMsg)> setpointSerializer;

    dmq::ScopedConnection tempConn;
    dmq::ScopedConnection setpointConn;
    dmq::ScopedConnection errorConn;
};

static bool SetupTransports(GatewayState& s, bool useShm) {
    if (s.udpIn.Create(UdpTransport::Type::SUB, SystemLink::UdpAddr, SystemLink::GatewayUdpPort) != 0) {
        std::cerr << "[gateway] Failed to create UDP SUB transport" << std::endl;
        return false;
    }
    s.udpIn.SetRecvTimeout(std::chrono::milliseconds(100));

    if (s.udpOut.Create(UdpTransport::Type::PUB, SystemLink::UdpAddr, SystemLink::SensorUdpPort) != 0) {
        std::cerr << "[gateway] Failed to create UDP PUB transport" << std::endl;
        return false;
    }

    if (useShm) {
        // Side A: create and initialize the shared region the monitor attaches to
        if (s.shm.Create(SystemLink::ShmName, ShmTransport::Side::A) != 0) {
            std::cerr << "[gateway] Failed to create shared memory link " << SystemLink::ShmName << std::endl;
            return false;
        }
        s.shm.SetSendTransport(nullptr);    // no ACKs: everything is sent unreliable
        s.ipc = &s.shm;
        return true;
    }

#ifdef _WIN32
    // The gateway is the pipe server: it creates the pipe the monitor connects to.
    if (s.pipe.Create(IpcTransport::Type::SUB, SystemLink::PipeName) != 0) {
        std::cerr << "[gateway] Failed to create named pipe " << SystemLink::PipeName << std::endl;
        return false;
    }
#else
    // Bind the gateway's socket path and send to the monitor's. No connection:
    // either process may start first.
    if (s.pipe.Create(IpcTransport::Type::SUB, SystemLink::GatewaySocketPath,
                      SystemLink::MonitorSocketPath) != 0) {
        std::cerr << "[gateway] Failed to create Unix socket " << SystemLink::GatewaySocketPath << std::endl;
        return false;
    }
#endif
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

    s.pipeParticipant = std::make_shared<Participant>(*s.ipc);
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
                  << ")  UDP -> " << g_ipcName << std::endl;
    });
    s.setpointConn = DataBus::Subscribe<SetpointMsg>(SystemTopic::CmdSetpoint, [](const SetpointMsg& msg) {
        std::cout << "[gateway] relayed cmd/setpoint: " << msg.celsius << " C  " << g_ipcName << " -> UDP" << std::endl;
    });

    // Until the monitor connects, IPC sends fail. Report once instead of every message.
    s.errorConn = DataBus::SubscribeError([](const dmq::xstring& topic, dmq::DelegateError err) {
        static std::atomic<bool> reported{ false };
        if (!reported.exchange(true)) {
            std::cout << "[gateway] send error on " << topic << " (error " << static_cast<int>(err)
                      << "); monitor not connected yet? Further errors not shown." << std::endl;
        }
    });
}

int main(int argc, char* argv[]) {
    const NodeArgs args = ParseNodeArgs(argc, argv);   // [seconds] [--shm]
    const int duration = args.duration;
    if (args.shm)
        g_ipcName = "shm";

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "Starting DataBus Relay GATEWAY (B, UDP <-> " << g_ipcName << ")..." << std::endl;

    dmq::util::NetworkContext netCtx;  // Winsock init on Windows; no-op on Linux

    GatewayState s;
    if (!SetupTransports(s, args.shm)) return -1;
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
            // Drain. Pipe/socket reads don't block, so pause between drains;
            // a shared-memory read sleeps until the monitor's next send (or
            // the 100 ms receive timeout), woken by its event or semaphore.
            while (s.pipeParticipant->ProcessIncoming() == 0) {}
            if (!args.shm)
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
    s.shm.Close();
    return 0;
}
