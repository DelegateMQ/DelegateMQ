// main.cpp
// DataBus Relay Example — MONITOR node (C).
//
//   sensor (A) <--- UDP ---> gateway (B) <--- named pipe ---> monitor (C)
//
// An ordinary DataBus node on the pipe side, typically a local app running on
// the same box as the gateway. It never talks to the sensor directly: the
// gateway relays sensor/temp in and cmd/setpoint out.
// - Subscribes to TempMsg and prints each reading.
// - Publishes a SetpointMsg every 3 seconds, alternating values. The sensor's
//   readings then drift toward the new setpoint (full round trip).

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
using dmq::transport::Win32PipeTransport;

static std::atomic<bool> g_running(true);

static void SignalHandler(int) { g_running = false; }

struct MonitorState {
    Win32PipeTransport pipe;   // client end of the gateway's named pipe (duplex)
    std::shared_ptr<Participant> pipeParticipant;
    dmq::serialization::serializer::Serializer<void(TempMsg)> tempSerializer;
    dmq::serialization::serializer::Serializer<void(SetpointMsg)> setpointSerializer;
    dmq::ScopedConnection tempConn;
    dmq::ScopedConnection errorConn;
};

// The pipe exists only once the gateway has created it, so retry for a while.
static bool ConnectPipe(MonitorState& s) {
    for (int attempt = 0; attempt < 40 && g_running; ++attempt) {
        if (s.pipe.Create(Win32PipeTransport::Type::PUB, SystemLink::PipeName) == 0) {
            std::cout << "[monitor] connected to gateway pipe " << SystemLink::PipeName << std::endl;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    std::cerr << "[monitor] Could not connect to " << SystemLink::PipeName
              << " (is the gateway running?)" << std::endl;
    return false;
}

static void SetupDataBus(MonitorState& s) {
    s.pipeParticipant = std::make_shared<Participant>(s.pipe);

    // Outgoing: cmd/setpoint -> gateway
    s.pipeParticipant->AddRemoteTopic(SystemTopic::CmdSetpoint, SystemTopic::CmdSetpointId);
    DataBus::AddParticipant(s.pipeParticipant);
    DataBus::RegisterSerializer<SetpointMsg>(SystemTopic::CmdSetpoint, s.setpointSerializer);

    // Incoming: sensor/temp relayed by the gateway, delivered locally only
    DataBus::AddIncomingTopic<TempMsg>(SystemTopic::SensorTemp, SystemTopic::SensorTempId,
        *s.pipeParticipant, s.tempSerializer);

    s.tempConn = DataBus::Subscribe<TempMsg>(SystemTopic::SensorTemp, [](const TempMsg& msg) {
        std::cout << "[monitor] sensor/temp received: " << msg.celsius << " C (seq " << msg.seq << ")" << std::endl;
    });

    // Transport/serialization errors, or a message arriving for an unknown remote ID.
    s.errorConn = DataBus::SubscribeError([](const dmq::xstring& topic, dmq::DelegateError err) {
        std::cout << "[monitor] DataBus error on '" << topic << "': " << static_cast<int>(err) << std::endl;
    });
}

int main(int argc, char* argv[]) {
    int duration = 0;
    if (argc > 1) duration = atoi(argv[1]);

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "Starting DataBus Relay MONITOR (C, pipe side)..." << std::endl;

    MonitorState s;
    if (!ConnectPipe(s)) return -1;
    SetupDataBus(s);

    std::atomic<bool> running{ true };
    std::thread recvThread([&]() {
        while (running) {
            while (s.pipeParticipant->ProcessIncoming() == 0) {}  // drain; pipe reads don't block
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

    // Main loop: send a new setpoint every 3 seconds, alternating warm/cool
    const float setpoints[] = { 30.0f, 18.0f };
    int idx = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (!g_running) break;

        SetpointMsg msg;
        msg.celsius = setpoints[idx];
        idx = (idx + 1) % 2;
        std::cout << "[monitor] publish cmd/setpoint: " << msg.celsius << " C" << std::endl;
        DataBus::Publish<SetpointMsg>(SystemTopic::CmdSetpoint, msg);
    }

    std::cout << "\n[monitor] Shutting down..." << std::endl;
    running = false;
    recvThread.join();
    s.pipe.Close();
    return 0;
}
