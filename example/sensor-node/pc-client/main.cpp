/// @file main.cpp
/// @brief PC client for the sensor node: receives its topics over UDP (DataBus),
/// sends it commands, and checks the round trip.
///
///   sensor_pc [seconds] [node-address]
///
/// It sends the node a Start every second: the node publishes only while they
/// keep coming, every intervalMs (the stats window) it asks for.
///
/// It always runs the self-test first (data arrives and is right, a rate and an
/// LED command are applied, the Start's interval sets the stats window,
/// touches arrive) and prints ALL TESTS PASSED or the failed checks. With seconds > 0 it then exits (the exit code is the result);
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
#include <random>
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
        if (verbose && m_view.stats % 10 == 0)
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
            printf("status #%u  up %u ms  %u Hz  stats every %u ms  LED %u,%u,%u  %u commands  %u dropped\n",
                   static_cast<unsigned>(m.seq), static_cast<unsigned>(m.uptimeMs), static_cast<unsigned>(m.rateHz),
                   static_cast<unsigned>(m.intervalMs), m.ledR, m.ledG, m.ledB, static_cast<unsigned>(m.commands), static_cast<unsigned>(m.netDropped));
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

/// One stats window of the node's 10 Hz test sine: whole periods (the window
/// is a multiple of 100 ms), so mean 0 and extremes near -1 and 1
bool SineWindowOk(const SignalStats& s, uint32_t rateHz, uint32_t intervalMs)
{
    return s.rateHz == rateHz && s.count == rateHz * intervalMs / 1000 && s.max > 0.9f && s.min < -0.9f
        && std::fabs(s.mean) < 0.05f;
}

const char* CommandName(dmq::DelegateRemoteId id)
{
    switch (id) {
    case SensorTopic::SetRateId: return SensorTopic::SetRate;
    case SensorTopic::SetLedId:  return SensorTopic::SetLed;
    case SensorTopic::StartId:   return SensorTopic::Start;
    default:                     return "?";
    }
}

/// What went wrong in the network layers, as DelegateMQ reports it through its
/// signals. The handlers run on whichever thread raised them (mostly the
/// pump), so the counts are atomic. Each kind prints its first few events.
class ErrorReport {
public:
    static constexpr uint32_t PRINT_LIMIT = 5;

    std::atomic<uint32_t> ackTimeouts{0};       // no ACK in time: being retried
    std::atomic<uint32_t> deliveryFailed{0};    // retries used up: the command is lost
    std::atomic<uint32_t> databusErrors{0};     // serialize, deserialize, dispatch, ...
    std::atomic<uint32_t> monitorOverflow{0};   // too many messages awaiting an ACK
    std::atomic<uint32_t> receiveDropped{0};    // the receive thread's queue was full

    void OnSendStatus(dmq::DelegateRemoteId id, uint16_t seq, TransportMonitor::Status status) {
        if (status == TransportMonitor::Status::TIMEOUT && ++ackTimeouts <= PRINT_LIMIT)
            printf("  [error] %s (transport seq %u): no ACK in time, retrying\n", CommandName(id), static_cast<unsigned>(seq));
    }
    void OnDeliveryFailed(dmq::DelegateRemoteId id, uint16_t seq) {
        if (++deliveryFailed <= PRINT_LIMIT)
            printf("  [error] %s (transport seq %u): not delivered, retries used up\n", CommandName(id), static_cast<unsigned>(seq));
    }
    void OnDataBusError(const dmq::xstring& topic, dmq::DelegateError error) {
        if (++databusErrors <= PRINT_LIMIT)
            printf("  [error] DataBus, topic %s: error %d\n", topic.c_str(), static_cast<int>(error));
    }
    void OnMonitorOverflow(size_t count) {
        if (++monitorOverflow <= PRINT_LIMIT)
            printf("  [error] transport monitor: %u messages awaiting an ACK, over its limit\n", static_cast<unsigned>(count));
    }
    void OnReceiveDropped(size_t count) {
        receiveDropped += static_cast<uint32_t>(count);
        printf("  [error] receive thread dropped %u messages\n", static_cast<unsigned>(count));
    }
};

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

    // Commands are reliable: a command not ACKed within CMD_ACK_TIMEOUT is sent
    // again, up to CMD_RETRIES times (RetryMonitor). The node's data is ACKed
    // but not retried: the next window replaces a lost one.
    TransportMonitor monitor{ SensorNet::CMD_ACK_TIMEOUT };
    RetryMonitor retry(transportCmd, monitor, SensorNet::CMD_RETRIES);
    ReliableTransport reliable(transportCmd, retry);
    transportCmd.SetTransportMonitor(&monitor);
    transportData.SetTransportMonitor(&monitor);
    transportData.SetSendTransport(&transportCmd);

    // Error reporting: DelegateMQ reports through signals; nothing is fatal here,
    // the self-test checks the counts at the end
    ErrorReport errors;
    ScopedConnection sendStatusConn = monitor.OnSendStatus.Connect(MakeDelegate(&errors, &ErrorReport::OnSendStatus));
    ScopedConnection capConn = monitor.OnCapExceeded.Connect(MakeDelegate(&errors, &ErrorReport::OnMonitorOverflow));
    ScopedConnection pendingConn = monitor.OnPendingExceeded.Connect(MakeDelegate(&errors, &ErrorReport::OnMonitorOverflow));
    ScopedConnection deliveryConn = retry.OnDeliveryFailed.Connect(MakeDelegate(&errors, &ErrorReport::OnDeliveryFailed));
    ScopedConnection databusErrorConn = DataBus::SubscribeError(MakeDelegate(&errors, &ErrorReport::OnDataBusError));

    serialization::serializer::Serializer<void(SignalStats)> statsSerializer;
    serialization::serializer::Serializer<void(Temperature)> tempSerializer;
    serialization::serializer::Serializer<void(Touch)> touchSerializer;
    serialization::serializer::Serializer<void(NodeStatus)> statusSerializer;
    serialization::serializer::Serializer<void(SetRate)> setRateSerializer;
    serialization::serializer::Serializer<void(SetLed)> setLedSerializer;
    serialization::serializer::Serializer<void(StartPublishing)> startSerializer;

    auto cmdParticipant = std::make_shared<Participant>(reliable);
    cmdParticipant->AddRemoteTopic(SensorTopic::SetRate, SensorTopic::SetRateId);
    cmdParticipant->AddRemoteTopic(SensorTopic::SetLed, SensorTopic::SetLedId);
    cmdParticipant->AddRemoteTopic(SensorTopic::Start, SensorTopic::StartId);
    DataBus::AddParticipant(cmdParticipant);
    DataBus::RegisterSerializer<SetRate>(SensorTopic::SetRate, setRateSerializer);
    DataBus::RegisterSerializer<SetLed>(SensorTopic::SetLed, setLedSerializer);
    DataBus::RegisterSerializer<StartPublishing>(SensorTopic::Start, startSerializer);

    auto dataParticipant = std::make_shared<Participant>(transportData);
    DataBus::AddIncomingTopic<SignalStats>(SensorTopic::SignalStats, SensorTopic::SignalStatsId, *dataParticipant, statsSerializer);
    DataBus::AddIncomingTopic<Temperature>(SensorTopic::Temperature, SensorTopic::TemperatureId, *dataParticipant, tempSerializer);
    DataBus::AddIncomingTopic<Touch>(SensorTopic::Touch, SensorTopic::TouchId, *dataParticipant, touchSerializer);
    DataBus::AddIncomingTopic<NodeStatus>(SensorTopic::Status, SensorTopic::StatusId, *dataParticipant, statusSerializer);

    // Each topic is delivered to the receive thread, which owns the NodeView
    dmq::os::Thread receiveThread("PcReceive");
    receiveThread.SetDroppedHandler(MakeDelegate(&errors, &ErrorReport::OnReceiveDropped));
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

    // Network pump: incoming topics, command ACKs, retries, and a Start every
    // second. The node publishes only while Starts keep coming (a lease), so
    // it falls silent a few seconds after this client exits.
    std::atomic<bool> pumping{true};
    std::atomic<uint32_t> intervalMs{100};      // stats window asked for in each Start
    // This run's session: our seq counts from 1, so a new session tells the
    // node to forget the last client's seq
    const uint32_t session = std::random_device{}() | 1u;    // never 0, the node's initial value
    std::thread pump([&] {
        auto nextStart = std::chrono::steady_clock::now();
        while (pumping) {
            if (std::chrono::steady_clock::now() >= nextStart) {
                StartPublishing start;      // a new message: a new seq (MessageBase)
                start.session = session;
                start.intervalMs = intervalMs;
                DataBus::Publish<StartPublishing>(SensorTopic::Start, start);
                nextStart += std::chrono::seconds(1);
            }
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
    // Also wait for this client's 100 ms interval to take effect: a node that
    // ran an earlier client may still use that one's interval at first
    Check(WaitFor(receiver, receiveThread, [](const NodeView& v) {
        return v.stats >= 3 && v.temperatures >= 1 && v.statuses >= 1 && v.lastStatus.intervalMs == 100
            && v.lastStats.count == v.lastStats.rateHz / 10; }, std::chrono::seconds(5)),
        "stats, temperature and status received");
    NodeView view = MakeDelegate(&receiver, &Receiver::Snapshot, receiveThread, WAIT_INFINITE)();
    const uint32_t startRate = view.lastStatus.rateHz;
    const uint32_t commandsBefore = view.lastStatus.commands;  // a board may have run other clients
    Check(startRate > 0 && SineWindowOk(view.lastStats, startRate, 100), "a stats window holds one period of the test sine");
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
    Check(SineWindowOk(view.lastStats, newRate, 100), "a window at the new rate holds one period of the sine");

    // ----------------------------------------------------------------------
    printf("\nTest 3: an LED command is applied\n");
    SetLed led;
    led.r = 10; led.g = 20; led.b = 30;
    DataBus::Publish<SetLed>(SensorTopic::SetLed, led);
    Check(WaitFor(receiver, receiveThread, [](const NodeView& v) {
        return v.lastStatus.ledR == 10 && v.lastStatus.ledG == 20 && v.lastStatus.ledB == 30; },
        std::chrono::seconds(3)), "the node reports the new LED colour");

    // What a retry after a lost ACK looks like to the node: the same message,
    // so the same seq (MessageBase stamps it at construction), again
    printf("  sending the same LED command again, same seq (a retry)\n");
    DataBus::Publish<SetLed>(SensorTopic::SetLed, led);

    // ----------------------------------------------------------------------
    printf("\nTest 4: the Start command's interval sets the stats window\n");
    intervalMs = 200;       // sent with the next Start, within a second
    Check(WaitFor(receiver, receiveThread, [newRate](const NodeView& v) {
        return v.lastStatus.intervalMs == 200 && v.lastStats.count == newRate * 200 / 1000; },
        std::chrono::seconds(4)), "the node reports 200 ms, and its windows are 200 ms long");
    view = MakeDelegate(&receiver, &Receiver::Snapshot, receiveThread, WAIT_INFINITE)();
    Check(SineWindowOk(view.lastStats, newRate, 200), "a 200 ms window holds two periods of the sine");

    // ----------------------------------------------------------------------
    printf("\nTest 5: touches arrive (from the touch interrupt)\n");
    // The desktop node simulates touches; on a board a person presses the pad
    // (on the ESP32-S3 build, the BOOT button)
    const bool localNode = nodeAddress == "127.0.0.1" || nodeAddress == "localhost";
    if (!localNode)
        printf("  Press and release the node's touch pad (ESP32-S3: the BOOT button) within 30 s\n");
    const uint32_t touchesBefore = MakeDelegate(&receiver, &Receiver::Snapshot, receiveThread, WAIT_INFINITE)().touches;
    Check(WaitFor(receiver, receiveThread, [touchesBefore](const NodeView& v) { return v.touches >= touchesBefore + 2; },
        localNode ? std::chrono::seconds(3) : std::chrono::seconds(30)), "touch changes received");

    view = MakeDelegate(&receiver, &Receiver::Snapshot, receiveThread, WAIT_INFINITE)();
    Check(view.lastStatus.commands == commandsBefore + 2,
        "the node applied the rate and the LED command once each (the repeated seq was ignored)");
    Check(view.lastStatus.netDropped == 0, "the node's network thread dropped nothing");
    Check(view.lastStatus.crossCoreUs > 0.0f && view.lastStatus.sameCoreUs > 0.0f,
        "the node measured its cross-core and same-core call latency");
    printf("  node's async-wait round trip: %.1f us to the other core, %.1f us within one\n",
           view.lastStatus.crossCoreUs, view.lastStatus.sameCoreUs);

    // ----------------------------------------------------------------------
    printf("\nTest 6: a command nobody ACKs is reported as not delivered\n");
    constexpr const char* DEAD_TOPIC = "test/unacked";
    constexpr dmq::DelegateRemoteId DEAD_ID = 299;
    constexpr uint16_t DEAD_PORT = SensorNet::CMD_PORT + 50;    // nobody listens there
    serialization::serializer::Serializer<void(SetLed)> deadSerializer;   // DataBus keeps a pointer to it
    DataBus::RegisterSerializer<SetLed>(DEAD_TOPIC, deadSerializer);
    {
        // Its own transport, monitor and participant, sending to a local port
        // nobody listens on, on a topic only it carries: the failure this
        // provokes stays out of the counts the real link is checked against
        transport::UdpTransport deadTransport;
        Check(deadTransport.Create(transport::UdpTransport::Type::PUB, "127.0.0.1", DEAD_PORT) == 0,
            "a transport to a port nobody listens on");
        deadTransport.SetRecvTimeout(std::chrono::milliseconds(1));
        TransportMonitor deadMonitor{ SensorNet::CMD_ACK_TIMEOUT };
        RetryMonitor deadRetry(deadTransport, deadMonitor, SensorNet::CMD_RETRIES);
        ReliableTransport deadReliable(deadTransport, deadRetry);
        deadTransport.SetTransportMonitor(&deadMonitor);
        std::atomic<uint32_t> failed{0};
        ScopedConnection failedConn = deadRetry.OnDeliveryFailed.Connect(MakeDelegate(
            [&failed](dmq::DelegateRemoteId, uint16_t) { failed++; }));

        auto deadParticipant = std::make_shared<Participant>(deadReliable);
        deadParticipant->AddRemoteTopic(DEAD_TOPIC, DEAD_ID);
        DataBus::AddParticipant(deadParticipant);

        DataBus::Publish<SetLed>(DEAD_TOPIC, SetLed{});
        const auto end = std::chrono::steady_clock::now()
            + SensorNet::CMD_RETRY_WINDOW + std::chrono::seconds(2);
        while (failed == 0 && std::chrono::steady_clock::now() < end) {
            deadMonitor.Process();      // times out the send, RetryMonitor re-sends
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(failed == 1, "RetryMonitor::OnDeliveryFailed fired once, after the retries");
        DataBus::RemoveParticipant(deadParticipant);
    }

    // ----------------------------------------------------------------------
    printf("\nErrors on the real link\n");
    printf("  ACK timeouts (retried): %u; delivery failures: %u; DataBus errors: %u; "
           "monitor overflows: %u; receive drops: %u\n",
           errors.ackTimeouts.load(), errors.deliveryFailed.load(), errors.databusErrors.load(),
           errors.monitorOverflow.load(), errors.receiveDropped.load());
    // A timeout that a retry then delivers is not a failure (Wi-Fi loses packets)
    Check(errors.deliveryFailed == 0, "every command was delivered");
    Check(errors.databusErrors == 0, "no DataBus errors (serialization, dispatch, ...)");
    Check(errors.monitorOverflow == 0 && errors.receiveDropped == 0, "nothing overflowed or was dropped");

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
