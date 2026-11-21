#ifndef SENSOR_NODE_H
#define SENSOR_NODE_H

/// @file SensorNode.h
/// @brief The sensor node's application: platform independent. It runs the
/// same on the desktop (platform/desktop) and on the ESP32-S3 (platform/esp32s3).
///
/// Three threads, each owning its own state, so no locks:
///
/// | Thread          | Core (ESP32-S3) | Work                                              |
/// |-----------------|-----------------|---------------------------------------------------|
/// | SampleThread    | 1               | Reads the fast signal every 10 ms; statistics per window (100 ms default) |
/// | ControlThread   | 1               | Temperature, touch, LED, sample rate, PC commands, status |
/// | NetThread       | 0               | DataBus over UDP: publishes to the PC, receives commands |
///
/// The threads talk only through DelegateMQ async calls: SampleThread and
/// ControlThread send their results to NetThread, which publishes them; the
/// PC's commands arrive on NetThread and the DataBus delivers them to
/// ControlThread. Touch changes come from an interrupt through an IsrDelegate.
/// NetThread drops (FullPolicy::DROP) rather than block the others when the
/// network is slow.
///
/// The node publishes only while a PC client is listening: the client sends
/// a Start every second, and the node stops publishing PC_TIMEOUT after the
/// last one. An idle node (a powered board with no client) sends nothing.
///
/// The platform's main loop must call dmq::util::Timer::ProcessTimers(),
/// dmq::util::IsrDelegates::ProcessAll() and dmq::os::Thread::WatchdogCheckAll().

#include "Hal.h"
#include "SensorMessages.h"
#include "SensorTopics.h"
#include "DelegateMQ.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace sensornode {

class SensorNode;

struct NodeConfig {
    std::string pcAddress = "127.0.0.1";    ///< where the node publishes
    uint32_t rateHz = 1000;                 ///< initial sample rate
    /// Cores for SampleThread and ControlThread, and for NetThread (see
    /// Thread::SetCoreAffinity(); 0 = any core). The ESP32-S3: 1 << 1 and 1 << 0.
    uint32_t appCoreMask = 0;
    uint32_t netCoreMask = 0;
    /// Optional, for the platform: RunNode() calls onStarted after Start() and
    /// onStopping before Stop(), on its own thread. The desktop starts
    /// DelegateMQ's diagnostic bridges (dmq-spy, dmq-monitor, dmq-thread) here.
    std::function<void(SensorNode&)> onStarted;
    std::function<void(SensorNode&)> onStopping;
};

/// Async-wait round trips from ControlThread, measured at Start()
struct LatencyStats {
    uint32_t calls = 0;
    float minUs = 0.0f;
    float meanUs = 0.0f;
    float maxUs = 0.0f;
};

/// Counters for the platform's summary and self-checks (any thread)
struct NodeCounters {
    uint32_t statsPublished = 0;
    uint32_t temperaturesPublished = 0;
    uint32_t touchesPublished = 0;
    uint32_t commands = 0;
    uint32_t netDropped = 0;
};

class SensorNode
{
public:
    static constexpr uint32_t MIN_RATE_HZ = 100;
    static constexpr uint32_t MAX_RATE_HZ = 10000;
    /// Publishing stops this long after the PC client's last Start
    static constexpr auto PC_TIMEOUT = std::chrono::seconds(5);
    /// The stats window (the Start command's intervalMs): whole 10 ms ticks,
    /// and whole periods of the 10 Hz test signal
    static constexpr uint32_t MIN_INTERVAL_MS = 100;
    static constexpr uint32_t MAX_INTERVAL_MS = 10000;

    explicit SensorNode(Platform& platform);
    ~SensorNode();

    SensorNode(const SensorNode&) = delete;
    SensorNode& operator=(const SensorNode&) = delete;

    /// Create the threads and the network, start sampling. Task context, once.
    void Start(const NodeConfig& config);

    /// Stop sampling and the network, exit the threads
    void Stop();

    NodeCounters GetCounters() const;

    /// Round trips measured at Start(): to NetThread (on the ESP32-S3, the other
    /// core) and to SampleThread (the same core as ControlThread)
    LatencyStats GetCrossCoreLatency() const { return m_crossCore; }
    LatencyStats GetSameCoreLatency() const { return m_sameCore; }

    /// For diagnostics (e.g. dmq::util::ThreadMonitor, NodeBridge::Watch()),
    /// valid between Start() and Stop()
    std::array<dmq::os::Thread*, 3> GetThreads() { return { &m_sampleThread, &m_controlThread, &m_netThread }; }
    dmq::util::TransportMonitor& GetTransportMonitor() { return m_monitor; }
    dmq::util::RetryMonitor* GetRetryMonitor() { return m_retry.get(); }

private:
    // SampleThread
    void OnSampleTick();
    void SetSampleRate(uint32_t rateHz);
    void SetWindow(uint32_t intervalMs);

    // ControlThread
    void OnControlTick();
    void OnTouch(uint8_t pad, uint8_t touched);
    void OnSetRate(const SetRate& cmd);
    void OnSetLed(const SetLed& cmd);
    void OnSetInterval(uint32_t intervalMs);
    void ResetCommandGuards();
    void SendStatus();
    void MeasureLatencies();
    LatencyStats MeasureRoundTrips(dmq::os::Thread& target, uint32_t calls);

    // NetThread
    void NetInit(std::string pcAddress);
    void NetShutdown();
    void OnNetTick();
    void OnStart(const StartPublishing& msg);
    void PublishStats(const SignalStats& msg);
    void PublishTemperature(const Temperature& msg);
    void PublishTouch(const Touch& msg);
    void PublishStatus(const NodeStatus& msg);

    // Any thread
    void OnNetDropped(size_t count);

    Platform& m_platform;

    dmq::os::Thread m_sampleThread;
    dmq::os::Thread m_controlThread;
    dmq::os::Thread m_netThread;

    ITouchInput::Sink m_touchSink;

    dmq::util::Timer m_sampleTimer;
    dmq::util::Timer m_controlTimer;
    dmq::util::Timer m_netTimer;
    dmq::ScopedConnection m_sampleConn;
    dmq::ScopedConnection m_controlConn;
    dmq::ScopedConnection m_netConn;
    dmq::ScopedConnection m_setRateConn;
    dmq::ScopedConnection m_setLedConn;
    dmq::ScopedConnection m_startConn;

    bool m_started = false;
    dmq::TimePoint m_startTime{};

    // SampleThread only
    uint32_t m_sampleRate = 0;
    uint32_t m_pendingRate = 0;
    uint32_t m_ticksPerWindow = 0;
    uint32_t m_pendingTicksPerWindow = 0;
    uint32_t m_windowTicks = 0;
    uint32_t m_windowCount = 0;
    float m_windowMin = 0.0f;
    float m_windowMax = 0.0f;
    double m_windowSum = 0.0;

    // ControlThread only
    uint32_t m_rateHz = 0;
    MessageGuard m_rateGuard;   // apply only newer commands (seq)
    MessageGuard m_ledGuard;
    uint32_t m_intervalMs = 0;
    SetLed m_led;
    LatencyStats m_crossCore;       // written at Start(), before the timers run
    LatencyStats m_sameCore;

    // NetThread only
    bool m_pcListening = false;         // a Start within PC_TIMEOUT
    dmq::TimePoint m_lastStart{};
    MessageGuard m_startGuard;
    uint32_t m_session = 0;             // the PC client run whose commands are accepted
    uint32_t m_requestedIntervalMs = 0;   // the last Start's, passed on to ControlThread
    dmq::transport::UdpTransport m_transportData;
    dmq::transport::UdpTransport m_transportCmd;
    dmq::util::TransportMonitor m_monitor{ std::chrono::seconds(1) };
    std::unique_ptr<dmq::util::RetryMonitor> m_retry;
    std::unique_ptr<dmq::util::ReliableTransport> m_reliable;
    std::shared_ptr<dmq::databus::Participant> m_dataParticipant;
    std::shared_ptr<dmq::databus::Participant> m_cmdParticipant;
    dmq::serialization::serializer::Serializer<void(SignalStats)> m_statsSerializer;
    dmq::serialization::serializer::Serializer<void(Temperature)> m_tempSerializer;
    dmq::serialization::serializer::Serializer<void(Touch)> m_touchSerializer;
    dmq::serialization::serializer::Serializer<void(NodeStatus)> m_statusSerializer;
    dmq::serialization::serializer::Serializer<void(SetRate)> m_setRateSerializer;
    dmq::serialization::serializer::Serializer<void(SetLed)> m_setLedSerializer;
    dmq::serialization::serializer::Serializer<void(StartPublishing)> m_startSerializer;

    // Any thread
    std::atomic<uint32_t> m_statsPublished{0};
    std::atomic<uint32_t> m_temperaturesPublished{0};
    std::atomic<uint32_t> m_touchesPublished{0};
    std::atomic<uint32_t> m_commands{0};
    std::atomic<uint32_t> m_netDropped{0};
};

} // namespace sensornode

#endif // SENSOR_NODE_H
