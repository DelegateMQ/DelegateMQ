// databus/local
//
// Single-process DataBus sample: no transport or serializer between the
// components. (With DMQ_DATABUS_TOOLS, on by default, SpyBridge also sends a
// stringified copy of every message to dmq-spy over UDP: optional, and not
// how the components talk.)
// Three active objects (Sensor, Controller, Logger) run on their own threads
// and communicate strictly through dmq::databus::DataBus:
//
//  1. Last-Value Cache (LVC) and lifespan: a late subscriber receives a fresh
//     cached setpoint immediately, while an expired cached value is skipped.
//  2. Asynchronous vs. synchronous delivery: Subscribe(..., &thread) queues
//     onto the subscriber's thread; Subscribe(..., nullptr) runs inline on
//     the publisher's thread.
//  3. Rate-limiting (QoS::minSeparation): Logger throttles high-rate sensor
//     readings while Controller sees every sample.
//  4. Content filtering (SubscribeFilter): an overheat alarm fires only when
//     tempC exceeds the alarm threshold.
//  5. Deadline monitoring (DeadlineSubscription): when Sensor stops publishing,
//     Controller's deadline expires and triggers a SafeShutdown command.
//  6. Bus diagnostics: SubscribeUnhandled catches a publish with no listeners,
//     SubscribeError guards against bus errors, and DataBus::Monitor / SpyBridge
//     observe formatted traffic through topic stringifiers (no serializers).
//
// NOTE: To watch live traffic in dmq-spy:
//   1. Build with -DDMQ_DATABUS_TOOLS=ON (default on Windows/Linux).
//   2. Start dmq-spy before running this app.

#include "DelegateMQ.h"
#include "Topics.h"
#ifdef DMQ_DATABUS_TOOLS
#include "SpyBridge.h"
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <optional>
#include <thread>

using namespace dmq;
using namespace dmq::databus;
using dmq::util::Timer;
using namespace std::chrono_literals;

static int s_failures = 0;

static void Check(bool ok, const char* what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        s_failures++;
}

//----------------------------------------------------------------------------
// Event-driven timer driver: sleeps until Timer::GetNextExpiry(), or until
// Timer::Start() schedules a timer due sooner.
//----------------------------------------------------------------------------
static std::mutex s_timerMutex;
static std::condition_variable s_timerCv;
static bool s_timerWake = false;
static bool s_timerExit = false;

static void WakeTimerDriver()
{
    {
        std::lock_guard<std::mutex> lock(s_timerMutex);
        s_timerWake = true;
    }
    s_timerCv.notify_one();
}

static void TimerDriver()
{
    for (;;)
    {
        Timer::ProcessTimers();
        const Duration next = Timer::GetNextExpiry();

        std::unique_lock<std::mutex> lock(s_timerMutex);
        auto woken = [] { return s_timerWake || s_timerExit; };
        if (next == Duration::max())
            s_timerCv.wait(lock, woken);
        else
            s_timerCv.wait_for(lock, next, woken);
        if (s_timerExit)
            return;
        s_timerWake = false;
    }
}

template <typename Pred>
static bool WaitFor(Pred&& pred, std::chrono::milliseconds timeout = 3000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

static void FlushThread(dmq::os::Thread& thread)
{
    MakeDelegate(+[] {}, thread, WAIT_INFINITE)();
}

//----------------------------------------------------------------------------
// Sensor: periodically publishes ramping temperature readings from its own
// thread. Completely unaware of who subscribes, so it could later move to
// another process or a second core (behind a future shared-memory or IPC link,
// with raw::Serializer for its topics) without changing its code.
//----------------------------------------------------------------------------
class Sensor
{
public:
    static constexpr uint32_t TOTAL_SAMPLES = 12;

    Sensor() : m_thread("SensorThread")
    {
        m_thread.CreateThread();
        m_timerConn = m_timer.OnExpired.Connect(
            dmq::util::MakeTimerDelegate(this, &Sensor::OnTick, m_thread));
    }

    ~Sensor()
    {
        Stop();
        m_timerConn.Disconnect();
        m_thread.ExitThread();
    }

    void Start(Duration period)
    {
        m_published.store(0);
        m_timer.Start(period, false); // periodic (once = false)
    }

    void Stop()
    {
        m_timer.Stop();
    }

    uint32_t PublishedCount() const { return m_published.load(); }

    dmq::os::Thread& GetThread() { return m_thread; }

private:
    void OnTick()
    {
        const uint32_t seq = m_published.load() + 1;
        if (seq > TOTAL_SAMPLES)
        {
            // Stop publishing so Controller's DeadlineSubscription expires.
            m_timer.Stop();
            return;
        }

        // Ramp 22.0C -> 33.0C in 1.0C steps (crosses 25.0C setpoint and 28.0C alarm)
        SensorReading reading{ seq, 21.0f + static_cast<float>(seq) };
        m_published.store(seq);
        std::printf("  [Sensor]     publish temp seq=%u (%.1fC)\n", reading.seq, reading.tempC);
        DataBus::Publish(topics::SensorTemp, reading);
    }

    dmq::os::Thread m_thread;
    Timer m_timer;
    ScopedConnection m_timerConn;
    std::atomic<uint32_t> m_published{ 0 };
};

//----------------------------------------------------------------------------
// Controller: subscribes late to config/setpoint (LVC), monitors sensor/temp
// with a DeadlineSubscription, and publishes ControlCmd on every reading or
// SafeShutdown if the sensor goes silent.
//----------------------------------------------------------------------------
class Controller
{
public:
    Controller() : m_thread("ControlThread")
    {
        m_thread.CreateThread();

        // Late subscriber with Last-Value Cache + generous lifespan: receives
        // the setpoint published before Controller was constructed.
        QoS lvcQos;
        lvcQos.lastValueCache = true;
        lvcQos.lifespan = 5s;
        m_setpointConn = DataBus::Subscribe<SetpointMsg>(
            topics::Setpoint,
            MakeDelegate(this, &Controller::OnSetpoint),
            &m_thread,
            lvcQos);
    }

    ~Controller()
    {
        // Order matters (DeadlineSubscription.h, "Lifetime"): its callbacks are
        // bound to its raw `this` and run on m_thread, so stop the thread first.
        // ExitThread() runs any callback already queued while m_deadline is
        // still alive; a publish or timer expiry after that is refused by the
        // exited thread (dropped, harmless at teardown). Destroying m_deadline
        // first would let a queued callback run on a freed object.
        m_setpointConn.Disconnect();
        m_thread.ExitThread();
        m_deadline.reset();
    }

    /// Start monitoring sensor/temp. Called when the Sensor starts publishing,
    /// not at construction: DeadlineSubscription arms its timer when created,
    /// so creating it earlier would count the wait before the first reading as
    /// a missed deadline.
    void StartMonitoring()
    {
        // Fires OnSensorTimeout if no reading arrives within 350 ms
        m_deadline.emplace(
            topics::SensorTemp,
            350ms,
            MakeDelegate(this, &Controller::OnReading),
            MakeDelegate(this, &Controller::OnSensorTimeout),
            &m_thread);
    }

    bool GotCachedSetpoint() const  { return m_gotSetpoint.load(); }
    bool SetpointOnOwnThread() const { return m_setpointOnOwnThread.load(); }
    uint32_t ReadingsReceived() const { return m_readings.load(); }
    bool AllReadingsOnOwnThread() const { return m_readingsOnOwnThread.load(); }
    uint32_t DeadlinesMissed() const { return m_deadlinesMissed.load(); }
    bool AllMissesOnOwnThread() const { return m_missesOnOwnThread.load(); }
    /// Readings received when SafeShutdown was published (0 if it never was)
    uint32_t ShutdownAfterReadings() const { return m_shutdownAfterReadings.load(); }

    dmq::os::Thread& GetThread() { return m_thread; }

private:
    void OnSetpoint(const SetpointMsg& sp)
    {
        m_setpoint = sp;
        m_setpointOnOwnThread.store(dmq::ThisThread::GetCurrent() == &m_thread);
        m_gotSetpoint.store(true);
        std::printf("  [Controller] got LVC setpoint: target=%.1fC alarm=%.1fC\n",
                    sp.targetC, sp.alarmC);
    }

    void OnReading(const SensorReading& r)
    {
        if (dmq::ThisThread::GetCurrent() != &m_thread)
            m_readingsOnOwnThread.store(false);

        m_lastTempC = r.tempC;
        m_readings.fetch_add(1);
        m_shutdownLatched = false;     // the sensor is alive: a later silence counts again

        HeaterMode mode = HeaterMode::Off;
        if (r.tempC < m_setpoint.targetC - 0.5f)
            mode = HeaterMode::Heat;
        else if (r.tempC > m_setpoint.targetC + 0.5f)
            mode = HeaterMode::Cool;

        ControlCmd cmd{ ++m_cmdSeq, mode, r.tempC };
        DataBus::Publish(topics::ControlCmd, cmd);
    }

    void OnSensorTimeout()
    {
        if (dmq::ThisThread::GetCurrent() != &m_thread)
            m_missesOnOwnThread.store(false);

        // The deadline timer keeps firing while the sensor stays silent:
        // publish SafeShutdown once per silence, not on every miss
        m_deadlinesMissed.fetch_add(1);
        if (!m_shutdownLatched)
        {
            m_shutdownLatched = true;
            m_shutdownAfterReadings.store(m_readings.load());
            std::printf("  [Controller] DEADLINE MISSED -- sensor silent, entering SafeShutdown\n");
            ControlCmd cmd{ ++m_cmdSeq, HeaterMode::SafeShutdown, m_lastTempC };
            DataBus::Publish(topics::ControlCmd, cmd);
        }
    }

    dmq::os::Thread m_thread;
    SetpointMsg m_setpoint{};
    float m_lastTempC = 0.0f;
    uint32_t m_cmdSeq = 0;
    bool m_shutdownLatched = false;     // ControlThread only

    std::atomic<bool> m_gotSetpoint{ false };
    std::atomic<bool> m_setpointOnOwnThread{ false };
    std::atomic<uint32_t> m_readings{ 0 };
    std::atomic<bool> m_readingsOnOwnThread{ true };
    std::atomic<uint32_t> m_deadlinesMissed{ 0 };
    std::atomic<bool> m_missesOnOwnThread{ true };
    std::atomic<uint32_t> m_shutdownAfterReadings{ 0 };

    ScopedConnection m_setpointConn;
    std::optional<DeadlineSubscription<SensorReading>> m_deadline;
};

//----------------------------------------------------------------------------
// Logger: demonstrates three subscription styles at once:
//  - Rate-limited async (QoS::minSeparation) on LoggerThread
//  - Filtered async (SubscribeFilter) on LoggerThread for overheat alarms
//  - Synchronous (thread = nullptr) on ControlCmd, running inline on the
//    publisher's thread (ControlThread)
//----------------------------------------------------------------------------
class Logger
{
public:
    explicit Logger(dmq::IThread* expectedCmdPublisher)
        : m_thread("LoggerThread")
        , m_expectedCmdPublisher(expectedCmdPublisher)
    {
        m_thread.CreateThread();

        // 1. Rate-limited async subscription: receive at most once per 250 ms
        QoS rateQos;
        rateQos.minSeparation = 250ms;
        m_tempConn = DataBus::Subscribe<SensorReading>(
            topics::SensorTemp,
            MakeDelegate(this, &Logger::OnThrottledTemp),
            &m_thread,
            rateQos);

        // 2. Filtered async subscription: only temperatures strictly above 28.0C
        m_alarmConn = DataBus::SubscribeFilter<SensorReading>(
            topics::SensorTemp,
            MakeDelegate(this, &Logger::OnOverTempAlarm),
            [](const SensorReading& r) { return r.tempC > 28.0f; },
            &m_thread);

        // 3. Synchronous subscription (nullptr thread): runs inline on publisher
        m_cmdConn = DataBus::Subscribe<ControlCmd>(
            topics::ControlCmd,
            MakeDelegate(this, &Logger::OnControlCmdSync),
            nullptr);
    }

    ~Logger()
    {
        m_tempConn.Disconnect();
        m_alarmConn.Disconnect();
        m_cmdConn.Disconnect();
        m_thread.ExitThread();
    }

    uint32_t ThrottledCount() const    { return m_throttledCount.load(); }
    bool ThrottledOnOwnThread() const  { return m_throttledOnOwnThread.load(); }
    uint32_t AlarmCount() const        { return m_alarmCount.load(); }
    bool AllAlarmsAboveLimit() const   { return m_allAlarmsAboveLimit.load(); }
    uint32_t SyncCmdCount() const      { return m_syncCmdCount.load(); }
    bool SyncCmdOnPublisher() const    { return m_syncCmdOnPublisher.load(); }
    bool SawSafeShutdown() const       { return m_sawSafeShutdown.load(); }
    /// lastTempC carried by the SafeShutdown command
    float ShutdownTempC() const        { return m_shutdownTempC.load(); }

    dmq::os::Thread& GetThread() { return m_thread; }

private:
    void OnThrottledTemp(const SensorReading& r)
    {
        if (dmq::ThisThread::GetCurrent() != &m_thread)
            m_throttledOnOwnThread.store(false);
        m_throttledCount.fetch_add(1);
        std::printf("  [Logger]     rate-limited temp: seq=%u (%.1fC)\n", r.seq, r.tempC);
    }

    void OnOverTempAlarm(const SensorReading& r)
    {
        if (r.tempC <= 28.0f)
            m_allAlarmsAboveLimit.store(false);
        m_alarmCount.fetch_add(1);
        std::printf("  [Alarm]      OVER-TEMP: seq=%u (%.1fC > 28.0C)\n", r.seq, r.tempC);
    }

    void OnControlCmdSync(const ControlCmd& cmd)
    {
        if (dmq::ThisThread::GetCurrent() != m_expectedCmdPublisher)
            m_syncCmdOnPublisher.store(false);
        m_syncCmdCount.fetch_add(1);
        if (cmd.mode == HeaterMode::SafeShutdown)
        {
            m_shutdownTempC.store(cmd.lastTempC);
            m_sawSafeShutdown.store(true);
        }
        std::printf("  [LoggerSync] cmd seq=%u mode=%s (lastTemp=%.1fC)\n",
                    cmd.seq, HeaterModeName(cmd.mode), cmd.lastTempC);
    }

    dmq::os::Thread m_thread;
    dmq::IThread* m_expectedCmdPublisher;

    std::atomic<uint32_t> m_throttledCount{ 0 };
    std::atomic<bool> m_throttledOnOwnThread{ true };
    std::atomic<uint32_t> m_alarmCount{ 0 };
    std::atomic<bool> m_allAlarmsAboveLimit{ true };
    std::atomic<uint32_t> m_syncCmdCount{ 0 };
    std::atomic<bool> m_syncCmdOnPublisher{ true };
    std::atomic<bool> m_sawSafeShutdown{ false };
    std::atomic<float> m_shutdownTempC{ 0.0f };

    ScopedConnection m_tempConn;
    ScopedConnection m_alarmConn;
    ScopedConnection m_cmdConn;
};

//----------------------------------------------------------------------------
// main
//----------------------------------------------------------------------------
int main()
{
    std::printf("DelegateMQ databus/local sample (single process: no transport or serializer between components)\n");

    // Start the event-driven timer pump for Sensor and DeadlineSubscription.
    Timer::SetScheduleChangedHandler(&WakeTimerDriver);
    std::thread timerDriver(TimerDriver);

    // Register stringifiers for DataBus::Monitor() and dmq-spy (no serializers).
    RegisterTopicStringifiers();

#ifdef DMQ_DATABUS_TOOLS
    SpyBridge::Start("127.0.0.1", 9999, "LocalNode");
#endif

    // Local bus diagnostics: monitor traffic, catch unhandled topics and errors.
    std::atomic<uint32_t> monitorPackets{ 0 };
    std::atomic<bool> monitorSawFormatted{ false };
    auto monitorConn = DataBus::Monitor([&](const SpyPacket& pkt) {
        monitorPackets.fetch_add(1);
        if (pkt.topic == topics::SensorTemp && pkt.value != "?")
            monitorSawFormatted.store(true);
    });

    // SubscribeUnhandled is connected after Phase 1 (below): seeding a
    // Last-Value Cache topic before anyone subscribes also counts as
    // "unhandled", though the value is cached for late subscribers.
    std::atomic<uint32_t> unhandledCount{ 0 };
    dmq::xstring lastUnhandledTopic;
    ScopedConnection unhandledConn;

    std::atomic<uint32_t> busErrors{ 0 };
    auto errorConn = DataBus::SubscribeError([&](const dmq::xstring& topic, DelegateError err) {
        busErrors.fetch_add(1);
        std::printf("  [Bus]        ERROR %d on topic '%s'\n", static_cast<int>(err), topic.c_str());
    });

    // -----------------------------------------------------------------------
    std::printf("\nPhase 1: Last-Value Cache (fresh vs. expired lifespan)\n");
    DataBus::LastValueCache(topics::Setpoint, true);
    DataBus::Publish(topics::Setpoint, SetpointMsg{ 25.0f, 28.0f });

    DataBus::LastValueCache(topics::Calibration, true);
    DataBus::Publish(topics::Calibration, CalibrationMsg{ 0.25f });
    std::this_thread::sleep_for(60ms);

    // The cached calibration is now 60 ms old. Two late synchronous
    // subscribers (the cached value is delivered inline, during Subscribe()):
    // one accepts values up to 5 s old, one only up to 20 ms. Delivering a
    // cached value doesn't consume it, so the order doesn't matter.
    bool freshCalDelivered = false;
    float freshCalOffset = 0.0f;
    {
        QoS longLvc;
        longLvc.lastValueCache = true;
        longLvc.lifespan = 5s;
        auto calConn = DataBus::Subscribe<CalibrationMsg>(
            topics::Calibration,
            [&](const CalibrationMsg& m) { freshCalDelivered = true; freshCalOffset = m.offsetC; },
            nullptr,
            longLvc);
    }
    Check(freshCalDelivered && freshCalOffset == 0.25f,
          "cached calibration (60 ms old) delivered to a 5 s lifespan subscriber");

    bool staleCalDelivered = false;
    {
        QoS shortLvc;
        shortLvc.lastValueCache = true;
        shortLvc.lifespan = 20ms;
        auto calConn = DataBus::Subscribe<CalibrationMsg>(
            topics::Calibration,
            [&](const CalibrationMsg&) { staleCalDelivered = true; },
            nullptr,
            shortLvc);
    }
    Check(!staleCalDelivered, "same cached value skipped by a 20 ms lifespan subscriber");

    unhandledConn = DataBus::SubscribeUnhandled([&](const dmq::xstring& topic) {
        unhandledCount.fetch_add(1);
        lastUnhandledTopic = topic;
        std::printf("  [Bus]        unhandled publish on topic '%s'\n", topic.c_str());
    });

    // -----------------------------------------------------------------------
    {
        // Construct subscribers AFTER Setpoint was published so Controller
        // exercises Last-Value Cache delivery on subscribe.
        Controller controller;
        Logger logger(&controller.GetThread());
        Sensor sensor;

        FlushThread(controller.GetThread());
        Check(controller.GotCachedSetpoint(), "late Controller received cached setpoint via LVC");
        Check(controller.SetpointOnOwnThread(), "LVC delivery ran on ControlThread");

        // -------------------------------------------------------------------
        std::printf("\nPhase 2: Active pub/sub (async, sync, rate-limit, filter, unhandled)\n");
        const uint32_t unhandledBefore = unhandledCount.load();
        DataBus::Publish(topics::Orphan, SensorReading{ 0, 0.0f });
        Check(unhandledCount.load() == unhandledBefore + 1 && lastUnhandledTopic == topics::Orphan,
              "SubscribeUnhandled fired for topic with no subscribers");

        controller.StartMonitoring();   // the deadline window starts with the sensor
        sensor.Start(80ms);

        // Wait until all 12 samples have been published and handled by Controller.
        Check(WaitFor([&] { return controller.ReadingsReceived() >= Sensor::TOTAL_SAMPLES; }),
              "Controller received all sensor readings");

        FlushThread(controller.GetThread());
        FlushThread(logger.GetThread());

        // -------------------------------------------------------------------
        std::printf("\nPhase 3: Sensor stops -> DeadlineSubscription fires SafeShutdown\n");
        sensor.Stop();

        Check(WaitFor([&] { return controller.DeadlinesMissed() >= 1 && logger.SawSafeShutdown(); }),
              "DeadlineSubscription detected silent sensor and triggered SafeShutdown");
        // The SafeShutdown came from this silence, not an early miss: it was
        // published after all readings, carrying the last one (33.0C)
        Check(controller.ShutdownAfterReadings() == Sensor::TOTAL_SAMPLES && logger.ShutdownTempC() == 33.0f,
              "SafeShutdown followed the last reading (12 readings, lastTemp 33.0C)");

        FlushThread(controller.GetThread());
        FlushThread(logger.GetThread());

        // -------------------------------------------------------------------
        std::printf("\nVerification:\n");
        const uint32_t ctrlCount = controller.ReadingsReceived();
        const uint32_t logCount  = logger.ThrottledCount();
        const uint32_t alarmCount = logger.AlarmCount();
        std::printf("  Controller readings=%u, Logger rate-limited=%u, Over-temp alarms=%u, Sync cmds=%u\n",
                    ctrlCount, logCount, alarmCount, logger.SyncCmdCount());

        Check(ctrlCount == Sensor::TOTAL_SAMPLES, "Controller got every reading (unthrottled)");
        Check(controller.AllReadingsOnOwnThread(), "Controller readings ran on ControlThread");
        Check(controller.DeadlinesMissed() >= 1 && controller.AllMissesOnOwnThread(),
              "Deadline missed callbacks all ran on ControlThread");
        Check(logCount >= 2 && logCount < ctrlCount, "Logger minSeparation throttled readings");
        Check(logger.ThrottledOnOwnThread(), "Logger rate-limited callbacks ran on LoggerThread");
        Check(alarmCount == 5 && logger.AllAlarmsAboveLimit(),
              "SubscribeFilter delivered only over-temp readings (29C..33C)");
        Check(logger.SyncCmdCount() >= ctrlCount + 1, "Sync subscriber received all ControlCmds");
        Check(logger.SyncCmdOnPublisher(), "Sync subscriber ran inline on publisher thread (ControlThread)");
        Check(monitorPackets.load() > ctrlCount && monitorSawFormatted.load(),
              "DataBus::Monitor observed stringified traffic without serializers");
        Check(busErrors.load() == 0, "no DataBus errors reported");

        // Stop the timer driver before objects tear down their Timers.
        Timer::SetScheduleChangedHandler(nullptr);
        {
            std::lock_guard<std::mutex> lock(s_timerMutex);
            s_timerExit = true;
        }
        s_timerCv.notify_one();
        timerDriver.join();
    }

    monitorConn.Disconnect();
    unhandledConn.Disconnect();
    errorConn.Disconnect();

#ifdef DMQ_DATABUS_TOOLS
    SpyBridge::Stop();
#endif

    if (s_failures == 0)
    {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d CHECK(S) FAILED\n", s_failures);
    return 1;
}
