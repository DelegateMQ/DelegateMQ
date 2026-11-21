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
/// | SampleThread    | 1               | Reads the fast signal every 10 ms; statistics per 100 ms window |
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
/// The platform's main loop must call dmq::util::Timer::ProcessTimers(),
/// dmq::util::IsrDelegates::ProcessAll() and dmq::os::Thread::WatchdogCheckAll().

#include "Hal.h"
#include "SensorMessages.h"
#include "SensorTopics.h"
#include "DelegateMQ.h"

#include <atomic>
#include <memory>
#include <string>

namespace sensornode {

struct NodeConfig {
    std::string pcAddress = "127.0.0.1";    ///< where the node publishes
    uint32_t rateHz = 1000;                 ///< initial sample rate
    /// Cores for SampleThread and ControlThread, and for NetThread (see
    /// Thread::SetCoreAffinity(); 0 = any core). The ESP32-S3: 1 << 1 and 1 << 0.
    uint32_t appCoreMask = 0;
    uint32_t netCoreMask = 0;
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

private:
    // SampleThread
    void OnSampleTick();
    void SetSampleRate(uint32_t rateHz);

    // ControlThread
    void OnControlTick();
    void OnTouch(uint8_t pad, uint8_t touched);
    void OnSetRate(const SetRate& cmd);
    void OnSetLed(const SetLed& cmd);
    void SendStatus();
    void MeasureLatencies();
    static LatencyStats MeasureRoundTrips(dmq::os::Thread& target, uint32_t calls);

    // NetThread
    void NetInit(std::string pcAddress);
    void NetShutdown();
    void OnNetTick();
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

    bool m_started = false;
    dmq::TimePoint m_startTime{};

    // SampleThread only
    uint32_t m_sampleRate = 0;
    uint32_t m_pendingRate = 0;
    uint32_t m_windowTicks = 0;
    uint32_t m_windowCount = 0;
    float m_windowMin = 0.0f;
    float m_windowMax = 0.0f;
    double m_windowSum = 0.0;
    uint32_t m_statsSeq = 0;

    // ControlThread only
    uint32_t m_rateHz = 0;
    SetLed m_led;
    uint32_t m_tempSeq = 0;
    uint32_t m_touchSeq = 0;
    uint32_t m_statusSeq = 0;
    LatencyStats m_crossCore;       // written at Start(), before the timers run
    LatencyStats m_sameCore;

    // NetThread only
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

    // Any thread
    std::atomic<uint32_t> m_statsPublished{0};
    std::atomic<uint32_t> m_temperaturesPublished{0};
    std::atomic<uint32_t> m_touchesPublished{0};
    std::atomic<uint32_t> m_commands{0};
    std::atomic<uint32_t> m_netDropped{0};
};

} // namespace sensornode

#endif // SENSOR_NODE_H
