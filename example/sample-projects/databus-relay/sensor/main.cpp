// main.cpp
// DataBus Relay Example — SENSOR node (A).
//
//   sensor (A) <--- UDP ---> gateway (B) <--- local IPC ---> monitor (C)
//
// An ordinary DataBus node on the Ethernet (UDP) side. It knows nothing about
// the pipe or the monitor: it publishes sensor/temp to the gateway and receives
// cmd/setpoint from it. The gateway relays both topics across to the monitor.
// - Publishes a simulated temperature (TempMsg) at 2 Hz.
// - Subscribes to SetpointMsg; the simulated temperature drifts toward it, so a
//   setpoint sent by the monitor shows up in later readings (full round trip).

#include "DelegateMQ.h"
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

static std::atomic<bool> g_running(true);
static std::atomic<float> g_setpoint{ 22.0f };

static void SignalHandler(int) { g_running = false; }

struct SensorState {
    UdpTransport transportOut;   // PUB -> gateway (sensor/temp)
    UdpTransport transportIn;    // SUB <- gateway (cmd/setpoint)
    std::shared_ptr<Participant> outParticipant;
    std::shared_ptr<Participant> inParticipant;
    dmq::serialization::serializer::Serializer<void(TempMsg)> tempSerializer;
    dmq::serialization::serializer::Serializer<void(SetpointMsg)> setpointSerializer;
    dmq::ScopedConnection setpointConn;
    dmq::ScopedConnection errorConn;
};

static bool Setup(SensorState& s) {
    if (s.transportOut.Create(UdpTransport::Type::PUB, SystemLink::UdpAddr, SystemLink::GatewayUdpPort) != 0) {
        std::cerr << "[sensor] Failed to create UDP PUB transport" << std::endl;
        return false;
    }
    if (s.transportIn.Create(UdpTransport::Type::SUB, SystemLink::UdpAddr, SystemLink::SensorUdpPort) != 0) {
        std::cerr << "[sensor] Failed to create UDP SUB transport" << std::endl;
        return false;
    }
    s.transportIn.SetRecvTimeout(std::chrono::milliseconds(100));

    // Outgoing: sensor/temp -> gateway
    s.outParticipant = std::make_shared<Participant>(s.transportOut);
    s.outParticipant->AddRemoteTopic(SystemTopic::SensorTemp, SystemTopic::SensorTempId);
    DataBus::AddParticipant(s.outParticipant);
    DataBus::RegisterSerializer<TempMsg>(SystemTopic::SensorTemp, s.tempSerializer);

    // Incoming: cmd/setpoint from gateway, delivered locally only
    s.inParticipant = std::make_shared<Participant>(s.transportIn);
    DataBus::AddIncomingTopic<SetpointMsg>(SystemTopic::CmdSetpoint, SystemTopic::CmdSetpointId,
        *s.inParticipant, s.setpointSerializer);

    s.setpointConn = DataBus::Subscribe<SetpointMsg>(SystemTopic::CmdSetpoint, [](const SetpointMsg& msg) {
        std::cout << "[sensor] setpoint received: " << msg.celsius << " C" << std::endl;
        g_setpoint = msg.celsius;
    });

    s.errorConn = DataBus::SubscribeError([](const dmq::xstring& topic, dmq::DelegateError err) {
        std::cout << "[sensor] DataBus error on '" << topic << "': " << static_cast<int>(err) << std::endl;
    });
    return true;
}

int main(int argc, char* argv[]) {
    int duration = 0;
    if (argc > 1) duration = atoi(argv[1]);

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "Starting DataBus Relay SENSOR (A, UDP side)..." << std::endl;

    dmq::util::NetworkContext netCtx;  // Winsock init on Windows; no-op on Linux

    SensorState s;
    if (!Setup(s)) return -1;

    // Receive thread: UDP SUB blocks up to its 100 ms receive timeout per call
    std::atomic<bool> running{ true };
    std::thread recvThread([&]() {
        while (running)
            s.inParticipant->ProcessIncoming();
    });

    if (duration > 0) {
        std::thread([duration]() {
            std::this_thread::sleep_for(std::chrono::seconds(duration));
            g_running = false;
        }).detach();
    } else {
        std::cout << "Press Ctrl+C to quit." << std::endl;
    }

    // Main loop: temperature drifts toward the current setpoint
    float temp = 20.0f;
    uint32_t seq = 0;
    while (g_running) {
        temp += (g_setpoint.load() - temp) * 0.25f;

        TempMsg msg;
        msg.celsius = temp;
        msg.seq = seq++;
        std::cout << "[sensor] publish sensor/temp: " << msg.celsius << " C (seq " << msg.seq << ")" << std::endl;
        DataBus::Publish<TempMsg>(SystemTopic::SensorTemp, msg);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\n[sensor] Shutting down..." << std::endl;
    running = false;
    recvThread.join();
    s.transportOut.Close();
    s.transportIn.Close();
    return 0;
}
