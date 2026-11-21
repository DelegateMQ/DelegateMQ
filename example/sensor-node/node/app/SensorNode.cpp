#include "SensorNode.h"

#include <array>
#include <cstdio>

using namespace dmq;
using namespace dmq::databus;
using namespace dmq::util;

namespace sensornode {

namespace {
    constexpr auto SAMPLE_PERIOD = std::chrono::milliseconds(10);   // one block per tick
    constexpr uint32_t TICKS_PER_SECOND = 100;
    constexpr uint32_t TICK_MS = 10;
    constexpr uint32_t DEFAULT_INTERVAL_MS = 100;                   // stats window until a Start sets one
    constexpr size_t MAX_BLOCK = SensorNode::MAX_RATE_HZ / TICKS_PER_SECOND;
    constexpr auto CONTROL_PERIOD = std::chrono::seconds(1);
    constexpr auto NET_PERIOD = std::chrono::milliseconds(10);
    constexpr auto WATCHDOG_TIMEOUT = std::chrono::seconds(2);
    constexpr size_t NET_QUEUE_SIZE = 64;

    bool ValidRate(uint32_t hz)
    {
        return hz >= SensorNode::MIN_RATE_HZ && hz <= SensorNode::MAX_RATE_HZ && hz % 100 == 0;
    }

    bool ValidInterval(uint32_t ms)
    {
        return ms >= SensorNode::MIN_INTERVAL_MS && ms <= SensorNode::MAX_INTERVAL_MS && ms % 100 == 0;
    }
}

SensorNode::SensorNode(Platform& platform) :
    m_platform(platform),
    m_sampleThread("SampleThread"),
    m_controlThread("ControlThread"),
    m_netThread("NetThread", NET_QUEUE_SIZE, FullPolicy::DROP),
    m_touchSink(MakeDelegate(this, &SensorNode::OnTouch, m_controlThread))
{
}

SensorNode::~SensorNode()
{
    Stop();
}

void SensorNode::Start(const NodeConfig& config)
{
    DMQ_ASSERT_TRUE(!m_started);
    m_started = true;
    m_startTime = Timer::GetNow();
    m_rateHz = ValidRate(config.rateHz) ? config.rateHz : 1000;
    m_sampleRate = m_rateHz;
    m_intervalMs = DEFAULT_INTERVAL_MS;
    m_ticksPerWindow = DEFAULT_INTERVAL_MS / TICK_MS;
    m_requestedIntervalMs = DEFAULT_INTERVAL_MS;

    // A slow network must not hold up sampling: count what NetThread drops
    m_netThread.SetDroppedHandler(MakeDelegate(this, &SensorNode::OnNetDropped));

    // Sampling and control on one core, the network on the other
    m_sampleThread.SetCoreAffinity(config.appCoreMask);
    m_controlThread.SetCoreAffinity(config.appCoreMask);
    m_netThread.SetCoreAffinity(config.netCoreMask);
    m_sampleThread.CreateThread(WATCHDOG_TIMEOUT);
    m_controlThread.CreateThread(WATCHDOG_TIMEOUT);
    m_netThread.CreateThread(WATCHDOG_TIMEOUT);

    // How long a DelegateMQ call takes to another core and back, and within one
    MakeDelegate(this, &SensorNode::MeasureLatencies, m_controlThread, WAIT_INFINITE)();

    // The network objects are created on, and only used by, NetThread
    MakeDelegate(this, &SensorNode::NetInit, m_netThread, WAIT_INFINITE)(config.pcAddress);

    // PC commands: the DataBus delivers them to ControlThread
    m_setRateConn = DataBus::Subscribe<SetRate>(SensorTopic::SetRate,
        [this](const SetRate& cmd) { OnSetRate(cmd); }, &m_controlThread);
    m_setLedConn = DataBus::Subscribe<SetLed>(SensorTopic::SetLed,
        [this](const SetLed& cmd) { OnSetLed(cmd); }, &m_controlThread);
    // The PC client's Start: NetThread, which decides whether to publish
    m_startConn = DataBus::Subscribe<StartPublishing>(SensorTopic::Start,
        [this](const StartPublishing& msg) { OnStart(msg); }, &m_netThread);

    m_platform.touch.Start(m_touchSink);

    m_sampleConn = m_sampleTimer.OnExpired.Connect(MakeTimerDelegate(this, &SensorNode::OnSampleTick, m_sampleThread));
    m_controlConn = m_controlTimer.OnExpired.Connect(MakeTimerDelegate(this, &SensorNode::OnControlTick, m_controlThread));
    m_netConn = m_netTimer.OnExpired.Connect(MakeTimerDelegate(this, &SensorNode::OnNetTick, m_netThread));
    m_sampleTimer.Start(SAMPLE_PERIOD);
    m_controlTimer.Start(CONTROL_PERIOD);
    m_netTimer.Start(NET_PERIOD);

    printf("SensorNode: async-wait round trip from ControlThread: to NetThread (other core) "
           "min %.1f / mean %.1f / max %.1f us; to SampleThread (same core) min %.1f / mean %.1f / max %.1f us\n",
           m_crossCore.minUs, m_crossCore.meanUs, m_crossCore.maxUs,
           m_sameCore.minUs, m_sameCore.meanUs, m_sameCore.maxUs);
    printf("SensorNode: sampling at %u Hz, publishing to %s:%u\n",
           static_cast<unsigned>(m_rateHz), config.pcAddress.c_str(), static_cast<unsigned>(SensorNet::DATA_PORT));
}

void SensorNode::Stop()
{
    if (!m_started)
        return;
    m_started = false;

    m_sampleTimer.Stop();
    m_controlTimer.Stop();
    m_netTimer.Stop();
    m_sampleConn.Disconnect();
    m_controlConn.Disconnect();
    m_netConn.Disconnect();
    m_platform.touch.Stop();
    m_setRateConn.Disconnect();
    m_setLedConn.Disconnect();
    m_startConn.Disconnect();

    MakeDelegate(this, &SensorNode::NetShutdown, m_netThread, WAIT_INFINITE)();

    m_sampleThread.ExitThread();
    m_controlThread.ExitThread();
    m_netThread.ExitThread();
}

NodeCounters SensorNode::GetCounters() const
{
    NodeCounters c;
    c.statsPublished = m_statsPublished;
    c.temperaturesPublished = m_temperaturesPublished;
    c.touchesPublished = m_touchesPublished;
    c.commands = m_commands;
    c.netDropped = m_netDropped;
    return c;
}

//----------------------------------------------------------------------------
// SampleThread
//----------------------------------------------------------------------------

void SensorNode::OnSampleTick()
{
    const size_t n = m_sampleRate / TICKS_PER_SECOND;
    std::array<float, MAX_BLOCK> block;
    m_platform.signal.Read(block.data(), n, m_sampleRate);

    for (size_t i = 0; i < n; i++) {
        const float v = block[i];
        if (m_windowCount == 0 || v < m_windowMin) m_windowMin = v;
        if (m_windowCount == 0 || v > m_windowMax) m_windowMax = v;
        m_windowSum += v;
        m_windowCount++;
    }

    if (++m_windowTicks < m_ticksPerWindow)
        return;

    SignalStats stats;
    stats.seq = ++m_statsSeq;
    stats.rateHz = m_sampleRate;
    stats.count = m_windowCount;
    stats.min = m_windowMin;
    stats.max = m_windowMax;
    stats.mean = m_windowCount ? static_cast<float>(m_windowSum / m_windowCount) : 0.0f;

    // Cross-thread (on the ESP32-S3, cross-core): NetThread publishes it
    MakeDelegate(this, &SensorNode::PublishStats, m_netThread)(stats);

    // Next window; a new rate takes effect at a window boundary
    m_windowTicks = 0;
    m_windowCount = 0;
    m_windowSum = 0.0;
    if (m_pendingRate != 0) {
        m_sampleRate = m_pendingRate;
        m_pendingRate = 0;
    }
    if (m_pendingTicksPerWindow != 0) {
        m_ticksPerWindow = m_pendingTicksPerWindow;
        m_pendingTicksPerWindow = 0;
    }
}

void SensorNode::SetWindow(uint32_t intervalMs)
{
    m_pendingTicksPerWindow = intervalMs / TICK_MS;
}

void SensorNode::SetSampleRate(uint32_t rateHz)
{
    m_pendingRate = rateHz;
}

//----------------------------------------------------------------------------
// ControlThread
//----------------------------------------------------------------------------

void SensorNode::OnControlTick()
{
    Temperature t;
    t.seq = ++m_tempSeq;
    t.celsius = m_platform.temperature.ReadCelsius();
    MakeDelegate(this, &SensorNode::PublishTemperature, m_netThread)(t);

    SendStatus();
}

void SensorNode::OnTouch(uint8_t pad, uint8_t touched)
{
    Touch msg;
    msg.seq = ++m_touchSeq;
    msg.pad = pad;
    msg.touched = touched;
    MakeDelegate(this, &SensorNode::PublishTouch, m_netThread)(msg);
}

// A retry repeats the command's seq: apply each seq once
static bool Duplicate(uint32_t seq, uint32_t& lastSeq, const char* command)
{
    if (seq == lastSeq) {
        printf("SensorNode: %s seq %u already applied (a retry)\n", command, static_cast<unsigned>(seq));
        return true;
    }
    lastSeq = seq;
    return false;
}

void SensorNode::OnSetRate(const SetRate& cmd)
{
    if (Duplicate(cmd.seq, m_lastRateSeq, "rate"))
        return;
    if (!ValidRate(cmd.rateHz)) {
        printf("SensorNode: rejected rate %u Hz (100-10000, a multiple of 100)\n", static_cast<unsigned>(cmd.rateHz));
        return;
    }
    m_rateHz = cmd.rateHz;
    m_commands++;
    printf("SensorNode: rate %u Hz\n", static_cast<unsigned>(m_rateHz));
    MakeDelegate(this, &SensorNode::SetSampleRate, m_sampleThread)(m_rateHz);
    SendStatus();
}

void SensorNode::OnSetLed(const SetLed& cmd)
{
    if (Duplicate(cmd.seq, m_lastLedSeq, "LED"))
        return;
    m_led = cmd;
    m_commands++;
    m_platform.led.Set(cmd.r, cmd.g, cmd.b);
    SendStatus();
}

// From NetThread, when a Start asks for a new interval
void SensorNode::OnSetInterval(uint32_t intervalMs)
{
    if (!ValidInterval(intervalMs) || intervalMs == m_intervalMs)
        return;
    m_intervalMs = intervalMs;
    printf("SensorNode: stats every %u ms\n", static_cast<unsigned>(m_intervalMs));
    MakeDelegate(this, &SensorNode::SetWindow, m_sampleThread)(m_intervalMs);
    SendStatus();
}

void SensorNode::SendStatus()
{
    NodeStatus s;
    s.seq = ++m_statusSeq;
    s.uptimeMs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Timer::GetNow() - m_startTime).count());
    s.rateHz = m_rateHz;
    s.intervalMs = m_intervalMs;
    s.ledR = m_led.r;
    s.ledG = m_led.g;
    s.ledB = m_led.b;
    s.commands = m_commands;
    s.netDropped = m_netDropped;
    s.crossCoreUs = m_crossCore.meanUs;
    s.sameCoreUs = m_sameCore.meanUs;
    MakeDelegate(this, &SensorNode::PublishStatus, m_netThread)(s);
}

namespace {
    void RoundTripTarget() {}
}

// ControlThread: async-wait calls to an empty function on target, timed
LatencyStats SensorNode::MeasureRoundTrips(dmq::os::Thread& target, uint32_t calls)
{
    using Clock = std::chrono::steady_clock;
    auto call = MakeDelegate(&RoundTripTarget, target, WAIT_INFINITE);
    call();     // warm up: first-use allocations, caches

    LatencyStats s;
    double sum = 0.0;
    for (uint32_t i = 0; i < calls; i++) {
        const auto start = Clock::now();
        call();
        const float us = std::chrono::duration<float, std::micro>(Clock::now() - start).count();
        if (i == 0 || us < s.minUs) s.minUs = us;
        if (i == 0 || us > s.maxUs) s.maxUs = us;
        sum += us;
    }
    s.calls = calls;
    s.meanUs = calls ? static_cast<float>(sum / calls) : 0.0f;
    return s;
}

void SensorNode::MeasureLatencies()
{
    constexpr uint32_t CALLS = 200;
    m_crossCore = MeasureRoundTrips(m_netThread, CALLS);
    m_sameCore = MeasureRoundTrips(m_sampleThread, CALLS);
}

//----------------------------------------------------------------------------
// NetThread
//----------------------------------------------------------------------------

void SensorNode::NetInit(std::string pcAddress)
{
    // Data to the PC (PUB), commands from it (SUB)
    DMQ_ASSERT_TRUE(m_transportData.Create(transport::UdpTransport::Type::PUB, pcAddress.c_str(), SensorNet::DATA_PORT) == 0);
    DMQ_ASSERT_TRUE(m_transportCmd.Create(transport::UdpTransport::Type::SUB, "0.0.0.0", SensorNet::CMD_PORT) == 0);

    // OnNetTick() polls both sockets every 10 ms on NetThread, which also
    // publishes: a receive must return at once when nothing is waiting. (A
    // SUB socket's default 2 s timeout suits a thread that only receives;
    // here it would stall NetThread past its watchdog. The PUB socket, which
    // receives the PC's ACKs, defaults to 2 ms on the desktop but 50 ms on lwIP.)
    m_transportCmd.SetRecvTimeout(std::chrono::milliseconds(1));
    m_transportData.SetRecvTimeout(std::chrono::milliseconds(1));

    // Reliable data (ACKed, retried) and ACKs for the PC's commands
    m_retry = std::make_unique<RetryMonitor>(m_transportData, m_monitor, 0);
    m_reliable = std::make_unique<ReliableTransport>(m_transportData, *m_retry);
    m_transportData.SetTransportMonitor(&m_monitor);
    m_transportCmd.SetTransportMonitor(&m_monitor);
    m_transportCmd.SetSendTransport(&m_transportData);

    m_dataParticipant = std::make_shared<Participant>(*m_reliable);
    m_dataParticipant->AddRemoteTopic(SensorTopic::SignalStats, SensorTopic::SignalStatsId);
    m_dataParticipant->AddRemoteTopic(SensorTopic::Temperature, SensorTopic::TemperatureId);
    m_dataParticipant->AddRemoteTopic(SensorTopic::Touch, SensorTopic::TouchId);
    m_dataParticipant->AddRemoteTopic(SensorTopic::Status, SensorTopic::StatusId);
    DataBus::AddParticipant(m_dataParticipant);
    DataBus::RegisterSerializer<SignalStats>(SensorTopic::SignalStats, m_statsSerializer);
    DataBus::RegisterSerializer<Temperature>(SensorTopic::Temperature, m_tempSerializer);
    DataBus::RegisterSerializer<Touch>(SensorTopic::Touch, m_touchSerializer);
    DataBus::RegisterSerializer<NodeStatus>(SensorTopic::Status, m_statusSerializer);

    m_cmdParticipant = std::make_shared<Participant>(m_transportCmd);
    DataBus::AddIncomingTopic<SetRate>(SensorTopic::SetRate, SensorTopic::SetRateId, *m_cmdParticipant, m_setRateSerializer);
    DataBus::AddIncomingTopic<SetLed>(SensorTopic::SetLed, SensorTopic::SetLedId, *m_cmdParticipant, m_setLedSerializer);
    DataBus::AddIncomingTopic<StartPublishing>(SensorTopic::Start, SensorTopic::StartId, *m_cmdParticipant, m_startSerializer);
    printf("SensorNode: waiting for a PC client (it sends a start every second)\n");
}

void SensorNode::NetShutdown()
{
    m_transportData.Close();
    m_transportCmd.Close();
}

void SensorNode::OnNetTick()
{
    // Incoming commands (republished on the local DataBus) and ACKs
    m_cmdParticipant->ProcessIncoming();
    m_dataParticipant->ProcessIncoming();
    m_monitor.Process();

    if (m_pcListening && Timer::GetNow() - m_lastStart > PC_TIMEOUT) {
        m_pcListening = false;
        printf("SensorNode: no start from the PC client for %u s: publishing paused\n",
               static_cast<unsigned>(std::chrono::duration_cast<std::chrono::seconds>(PC_TIMEOUT).count()));
    }
}

void SensorNode::OnStart(const StartPublishing& msg)
{
    m_lastStart = Timer::GetNow();
    // The interval belongs to ControlThread (it validates it and reports it in
    // the status); pass it on only when it changes, not with every Start
    if (msg.intervalMs != m_requestedIntervalMs) {
        m_requestedIntervalMs = msg.intervalMs;
        MakeDelegate(this, &SensorNode::OnSetInterval, m_controlThread)(msg.intervalMs);
    }
    if (!m_pcListening) {
        m_pcListening = true;
        printf("SensorNode: PC client listening: publishing\n");
    }
}

// Each Publish*() sends only while a PC client is listening
void SensorNode::PublishStats(const SignalStats& msg)
{
    if (!m_pcListening)
        return;
    DataBus::Publish<SignalStats>(SensorTopic::SignalStats, msg);
    m_statsPublished++;
}

void SensorNode::PublishTemperature(const Temperature& msg)
{
    if (!m_pcListening)
        return;
    DataBus::Publish<Temperature>(SensorTopic::Temperature, msg);
    m_temperaturesPublished++;
}

void SensorNode::PublishTouch(const Touch& msg)
{
    if (!m_pcListening)
        return;
    DataBus::Publish<Touch>(SensorTopic::Touch, msg);
    m_touchesPublished++;
}

void SensorNode::PublishStatus(const NodeStatus& msg)
{
    if (!m_pcListening)
        return;
    DataBus::Publish<NodeStatus>(SensorTopic::Status, msg);
}

//----------------------------------------------------------------------------
// Any thread
//----------------------------------------------------------------------------

void SensorNode::OnNetDropped(size_t count)
{
    m_netDropped += static_cast<uint32_t>(count);
}

} // namespace sensornode
