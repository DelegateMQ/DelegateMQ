/// @file main.cpp
/// @brief PC client for the sensor node: receives its topics over UDP (DataBus),
/// sends it commands, and checks the round trip.
///
///   sensor_pc [seconds] [node-address]
///
/// It always runs the self-test first (data arrives and is right, a rate and an
/// LED command are applied, touches arrive) and prints ALL TESTS PASSED or the
/// failed checks. With seconds > 0 it then exits (the exit code is the result);
/// with 0 (the default) it keeps printing what the node sends until Ctrl+C.
/// node-address: where to send commands (default 127.0.0.1).

#include "DelegateMQ.h"
#include "SensorMessages.h"
#include "SensorTopics.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using namespace dmq;
using namespace dmq::databus;
using namespace dmq::util;

namespace {

std::atomic<bool> s_running{true};
void OnSignal(int) { s_running = false; }

/// What the node has sent. Updated only on the PC's receive thread (the
/// DataBus delivers each topic there); read with an async-wait Snapshot().
struct NodeView {
    uint32_t stats = 0;
    SignalStats lastStats;
    uint32_t temperatures = 0;
    Temperature lastTemperature;
    uint32_t touches = 0;
    Touch lastTouch;
    uint32_t statuses = 0;
    NodeStatus lastStatus;
};

class Receiver {
public:
    bool verbose = false;

    void OnStats(const SignalStats& m) {
        m_view.stats++;
        m_view.lastStats = m;
        if (verbose && m.seq % 10 == 0)
            printf("stats  #%u  %u Hz  n=%u  min=%.3f max=%.3f mean=%.4f\n", static_cast<unsigned>(m.seq),
                   static_cast<unsigned>(m.rateHz), static_cast<unsigned>(m.count), m.min, m.max, m.mean);
    }
    void OnTemperature(const Temperature& m) {
        m_view.temperatures++;
        m_view.lastTemperature = m;
        if (verbose)
            printf("temp   #%u  %.2f C\n", static_cast<unsigned>(m.seq), m.celsius);
    }
    void OnTouch(const Touch& m) {
        m_view.touches++;
        m_view.lastTouch = m;
        if (verbose)
            printf("touch  #%u  pad %u %s\n", static_cast<unsigned>(m.seq), m.pad, m.touched ? "touched" : "released");
    }
    void OnStatus(const NodeStatus& m) {
        m_view.statuses++;
        m_view.lastStatus = m;
        if (verbose)
            printf("status #%u  up %u ms  %u Hz  LED %u,%u,%u  %u commands  %u dropped\n",
                   static_cast<unsigned>(m.seq), static_cast<unsigned>(m.uptimeMs), static_cast<unsigned>(m.rateHz),
                   m.ledR, m.ledG, m.ledB, static_cast<unsigned>(m.commands), static_cast<unsigned>(m.netDropped));
    }
    NodeView Snapshot() const { return m_view; }

private:
    NodeView m_view;
};

int s_failures = 0;

void Check(bool ok, const char* what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        s_failures++;
}

/// Poll the receiver until pred(view) holds, or the timeout
bool WaitFor(Receiver& receiver, dmq::os::Thread& thread, const std::function<bool(const NodeView&)>& pred,
             std::chrono::milliseconds timeout)
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (pred(MakeDelegate(&receiver, &Receiver::Snapshot, thread, WAIT_INFINITE)()))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

/// One window of the node's 10 Hz test sine: one full period
bool SineWindowOk(const SignalStats& s, uint32_t rateHz)
{
    return s.rateHz == rateHz && s.count == rateHz / 10 && s.max > 0.9f && s.min < -0.9f
        && std::fabs(s.mean) < 0.05f;
}

} // namespace

int main(int argc, char* argv[])
{
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 0;
    const std::string nodeAddress = argc > 2 ? argv[2] : "127.0.0.1";

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

#ifdef _WIN32
    NetworkContext winsock;
#endif

    // Data from the node (SUB), commands to it (PUB, ACKed and retried)
    transport::UdpTransport transportData;
    transport::UdpTransport transportCmd;
    if (transportData.Create(transport::UdpTransport::Type::SUB, "0.0.0.0", SensorNet::DATA_PORT) != 0 ||
        transportCmd.Create(transport::UdpTransport::Type::PUB, nodeAddress.c_str(), SensorNet::CMD_PORT) != 0) {
        printf("Failed to create the UDP transports\n");
        return 1;
    }
    // The pump below polls both sockets in turn: don't let an idle data socket
    // hold up the command ACKs for its default 2 s receive timeout
    transportData.SetRecvTimeout(std::chrono::milliseconds(2));

    TransportMonitor monitor{ std::chrono::seconds(1) };
    RetryMonitor retry(transportCmd, monitor, 0);
    ReliableTransport reliable(transportCmd, retry);
    transportCmd.SetTransportMonitor(&monitor);
    transportData.SetTransportMonitor(&monitor);
    transportData.SetSendTransport(&transportCmd);

    serialization::serializer::Serializer<void(SignalStats)> statsSerializer;
    serialization::serializer::Serializer<void(Temperature)> tempSerializer;
    serialization::serializer::Serializer<void(Touch)> touchSerializer;
    serialization::serializer::Serializer<void(NodeStatus)> statusSerializer;
    serialization::serializer::Serializer<void(SetRate)> setRateSerializer;
    serialization::serializer::Serializer<void(SetLed)> setLedSerializer;

    auto cmdParticipant = std::make_shared<Participant>(reliable);
    cmdParticipant->AddRemoteTopic(SensorTopic::SetRate, SensorTopic::SetRateId);
    cmdParticipant->AddRemoteTopic(SensorTopic::SetLed, SensorTopic::SetLedId);
    DataBus::AddParticipant(cmdParticipant);
    DataBus::RegisterSerializer<SetRate>(SensorTopic::SetRate, setRateSerializer);
    DataBus::RegisterSerializer<SetLed>(SensorTopic::SetLed, setLedSerializer);

    auto dataParticipant = std::make_shared<Participant>(transportData);
    DataBus::AddIncomingTopic<SignalStats>(SensorTopic::SignalStats, SensorTopic::SignalStatsId, *dataParticipant, statsSerializer);
    DataBus::AddIncomingTopic<Temperature>(SensorTopic::Temperature, SensorTopic::TemperatureId, *dataParticipant, tempSerializer);
    DataBus::AddIncomingTopic<Touch>(SensorTopic::Touch, SensorTopic::TouchId, *dataParticipant, touchSerializer);
    DataBus::AddIncomingTopic<NodeStatus>(SensorTopic::Status, SensorTopic::StatusId, *dataParticipant, statusSerializer);

    // Each topic is delivered to the receive thread, which owns the NodeView
    dmq::os::Thread receiveThread("PcReceive");
    receiveThread.CreateThread();
    Receiver receiver;
    receiver.verbose = seconds == 0;
    ScopedConnection statsConn = DataBus::Subscribe<SignalStats>(SensorTopic::SignalStats,
        [&receiver](const SignalStats& m) { receiver.OnStats(m); }, &receiveThread);
    ScopedConnection tempConn = DataBus::Subscribe<Temperature>(SensorTopic::Temperature,
        [&receiver](const Temperature& m) { receiver.OnTemperature(m); }, &receiveThread);
    ScopedConnection touchConn = DataBus::Subscribe<Touch>(SensorTopic::Touch,
        [&receiver](const Touch& m) { receiver.OnTouch(m); }, &receiveThread);
    ScopedConnection statusConn = DataBus::Subscribe<NodeStatus>(SensorTopic::Status,
        [&receiver](const NodeStatus& m) { receiver.OnStatus(m); }, &receiveThread);

    // Network pump: incoming topics, command ACKs, retries
    std::atomic<bool> pumping{true};
    std::thread pump([&] {
        while (pumping) {
            dataParticipant->ProcessIncoming();
            cmdParticipant->ProcessIncoming();
            monitor.Process();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    printf("Sensor PC client: listening on port %u, commands to %s:%u\n",
           static_cast<unsigned>(SensorNet::DATA_PORT), nodeAddress.c_str(), static_cast<unsigned>(SensorNet::CMD_PORT));

    // ----------------------------------------------------------------------
    printf("\nTest 1: data arrives from the node\n");
    Check(WaitFor(receiver, receiveThread, [](const NodeView& v) {
        return v.stats >= 3 && v.temperatures >= 1 && v.statuses >= 1; }, std::chrono::seconds(5)),
        "stats, temperature and status received");
    NodeView view = MakeDelegate(&receiver, &Receiver::Snapshot, receiveThread, WAIT_INFINITE)();
    const uint32_t startRate = view.lastStatus.rateHz;
    Check(startRate > 0 && SineWindowOk(view.lastStats, startRate), "a stats window holds one period of the test sine");
    Check(view.lastTemperature.celsius > 0.0f && view.lastTemperature.celsius < 100.0f, "temperature in range");

    // ----------------------------------------------------------------------
    printf("\nTest 2: a rate command is applied\n");
    const uint32_t newRate = startRate == 500 ? 2000 : 500;
    SetRate rate;
    rate.rateHz = newRate;
    DataBus::Publish<SetRate>(SensorTopic::SetRate, rate);
    Check(WaitFor(receiver, receiveThread, [newRate](const NodeView& v) {
        return v.lastStatus.rateHz == newRate && v.lastStats.rateHz == newRate; }, std::chrono::seconds(3)),
        "the node reports the new rate, and samples at it");
    view = MakeDelegate(&receiver, &Receiver::Snapshot, receiveThread, WAIT_INFINITE)();
    Check(SineWindowOk(view.lastStats, newRate), "a window at the new rate holds one period of the sine");

    // ----------------------------------------------------------------------
    printf("\nTest 3: an LED command is applied\n");
    SetLed led;
    led.r = 10; led.g = 20; led.b = 30;
    DataBus::Publish<SetLed>(SensorTopic::SetLed, led);
    Check(WaitFor(receiver, receiveThread, [](const NodeView& v) {
        return v.lastStatus.ledR == 10 && v.lastStatus.ledG == 20 && v.lastStatus.ledB == 30; },
        std::chrono::seconds(3)), "the node reports the new LED colour");

    // ----------------------------------------------------------------------
    printf("\nTest 4: touches arrive (from the touch interrupt)\n");
    Check(WaitFor(receiver, receiveThread, [](const NodeView& v) { return v.touches >= 2; },
        std::chrono::seconds(3)), "touch changes received");

    view = MakeDelegate(&receiver, &Receiver::Snapshot, receiveThread, WAIT_INFINITE)();
    Check(view.lastStatus.commands >= 2, "the node counted both commands");
    Check(view.lastStatus.netDropped == 0, "the node's network thread dropped nothing");
    Check(view.lastStatus.crossCoreUs > 0.0f && view.lastStatus.sameCoreUs > 0.0f,
        "the node measured its cross-core and same-core call latency");
    printf("  node's async-wait round trip: %.1f us to the other core, %.1f us within one\n",
           view.lastStatus.crossCoreUs, view.lastStatus.sameCoreUs);

    if (s_failures == 0)
        printf("\nALL TESTS PASSED\n");
    else
        printf("\n%d CHECK(S) FAILED\n", s_failures);

    // Interactive: keep showing what the node sends
    if (seconds == 0) {
        MakeDelegate(+[](Receiver* r) { r->verbose = true; }, receiveThread, WAIT_INFINITE)(&receiver);
        printf("\nShowing the node's data. Ctrl+C to stop.\n");
        while (s_running)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    statsConn.Disconnect();
    tempConn.Disconnect();
    touchConn.Disconnect();
    statusConn.Disconnect();
    pumping = false;
    pump.join();
    receiveThread.ExitThread();
    transportData.Close();
    transportCmd.Close();
    return s_failures == 0 ? 0 : 1;
}
