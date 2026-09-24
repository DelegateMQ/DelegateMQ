#include "SelfTest.h"
#include "system/System.h"
#include "util/Constants.h"
#include "messages/PumpCommandMsg.h"
#include "messages/PumpStatusMsg.h"
#include "messages/TelemetryMsg.h"
#include "messages/AlarmMsg.h"
#include "messages/HeartbeatMsg.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>

using namespace dmq;
using namespace dmq::databus;

namespace pumptron {
namespace gui {

namespace {

// Latest values seen on the bus. Handlers run synchronously on the link's
// receive thread, so everything shared with the test sequence is atomic.
std::atomic<uint32_t>  g_heartbeats{0};
std::atomic<uint32_t>  g_statusCount{0};
std::atomic<PumpState> g_state{PumpState::IDLE};
std::atomic<AlarmCode> g_fault{AlarmCode::NONE};
std::atomic<uint16_t>  g_setpoint{0};
std::atomic<float>     g_rpm{0.0f};
std::atomic<uint32_t>  g_telemetryCount{0};
std::atomic<bool>      g_alarmActive[static_cast<size_t>(AlarmCode::GUI_LINK_LOST) + 1];

int g_failures = 0;

// Controller-time / wall-time ratio correction. The FreeRTOS Windows simulator
// generates its tick with Sleep(1), which runs slower than real time, so every
// controller-side duration (priming, ramps, heartbeat) stretches accordingly.
float g_timeScale = 1.0f;

void Send(PumpCommand cmd, uint16_t rpm = 0) {
    DataBus::Publish<PumpCommandMsg>(topics::CMD, PumpCommandMsg(cmd, rpm));
}

bool WaitFor(const char* what, std::chrono::milliseconds timeout, const std::function<bool()>& cond) {
    timeout = std::chrono::milliseconds(static_cast<long long>(static_cast<float>(timeout.count()) * g_timeScale));
    const auto start = Clock::now();
    while (Clock::now() - start < timeout) {
        if (cond()) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            printf("  PASS  %-48s (%lld ms)\n", what, static_cast<long long>(ms));
            return true;
        }
        os::Thread::Sleep(std::chrono::milliseconds(20));
    }
    printf("  FAIL  %-48s (timed out after %lld ms)\n", what, static_cast<long long>(timeout.count()));
    g_failures++;
    return false;
}

bool StateIs(PumpState s) { return g_state.load() == s; }
bool AlarmIs(AlarmCode c, bool active) { return g_alarmActive[static_cast<size_t>(c)].load() == active; }
bool RpmNear(float target) { return std::fabs(g_rpm.load() - target) < 25.0f; }

} // namespace

int RunSelfTest()
{
    using namespace std::chrono_literals;
    for (auto& a : g_alarmActive) a = false;

    auto hbConn = DataBus::Subscribe<HeartbeatMsg>(topics::HB_CONTROLLER,
        [](const HeartbeatMsg&) { g_heartbeats++; });
    auto statusConn = DataBus::Subscribe<PumpStatusMsg>(topics::STATUS,
        [](const PumpStatusMsg& m) {
            g_state = m.state;
            g_fault = m.faultCode;
            g_setpoint = m.setpointRpm;
            g_statusCount++;
        });
    auto telemetryConn = DataBus::Subscribe<TelemetryMsg>(topics::TELEMETRY,
        [](const TelemetryMsg& m) { g_rpm = m.rpm; g_telemetryCount++; });
    auto alarmConn = DataBus::Subscribe<AlarmMsg>(topics::ALARM,
        [](const AlarmMsg& m) {
            const auto i = static_cast<size_t>(m.code);
            if (i < std::size(g_alarmActive)) g_alarmActive[i] = m.active;
        });

    printf("Pumptron self-test over %s\n", System::GetInstance().GetLinkDescription().c_str());

    // 1. Link up
    if (!WaitFor("controller heartbeat received", 10s, [] { return g_heartbeats > 0; })) {
        printf("Self-test aborted: no controller on the link.\n");
        return 1;
    }

    // Measure the controller's clock against wall time via its heartbeat rate
    // (nominally 1/HEARTBEAT_PERIOD) and stretch all later timeouts to match.
    {
        const uint32_t hb0 = g_heartbeats;
        const uint32_t tm0 = g_telemetryCount;
        os::Thread::Sleep(4s);
        const float rate = static_cast<float>(g_heartbeats - hb0) / 4.0f;
        printf("  INFO  telemetry %.1f/s, heartbeat %.2f/s over 4 s\n",
               static_cast<float>(g_telemetryCount - tm0) / 4.0f, rate);
        const float expected = 1.0f / std::chrono::duration<float>(HEARTBEAT_PERIOD).count();
        const float ratio = rate / expected;
        if (ratio < 0.2f) {
            printf("  FAIL  controller heartbeat rate %.2f/s (expected %.2f/s)\n", rate, expected);
            g_failures++;
        } else {
            g_timeScale = ratio < 0.9f ? 1.0f / ratio : 1.0f;
            printf("  INFO  controller clock runs at %.2fx real time%s\n", ratio,
                   ratio < 0.9f ? " (simulator tick) - scaling timeouts" : "");
        }
    }

    const uint32_t before = g_statusCount;
    Send(PumpCommand::QUERY);
    WaitFor("status reply to QUERY", 3s, [before] { return g_statusCount > before; });
    WaitFor("telemetry streaming", 2s, [] { return g_telemetryCount > 5; });

    // Leave any latched fault from a previous run.
    if (StateIs(PumpState::FAULT)) {
        WaitFor("pump coasted to stop", 5s, [] { return g_rpm < STOPPED_RPM; });
        Send(PumpCommand::RESET);
    }
    if (!StateIs(PumpState::IDLE)) {
        Send(PumpCommand::STOP);
        WaitFor("controller returned to IDLE", 10s, [] { return StateIs(PumpState::IDLE); });
    }

    // 2. Normal start -> run -> speed change -> stop
    Send(PumpCommand::SET_SPEED, 1200);
    WaitFor("setpoint 1200 acknowledged in status", 3s, [] { return g_setpoint == 1200; });
    Send(PumpCommand::START);
    WaitFor("START -> PRIMING", 3s, [] { return StateIs(PumpState::PRIMING); });
    WaitFor("PRIMING -> RUNNING", 6s, [] { return StateIs(PumpState::RUNNING); });
    WaitFor("speed reached 1200 RPM", 6s, [] { return RpmNear(1200.0f); });
    Send(PumpCommand::SET_SPEED, 2000);
    WaitFor("speed reached 2000 RPM", 6s, [] { return RpmNear(2000.0f); });
    Send(PumpCommand::STOP);
    WaitFor("STOP -> STOPPING", 3s, [] { return StateIs(PumpState::STOPPING); });
    WaitFor("STOPPING -> IDLE", 8s, [] { return StateIs(PumpState::IDLE); });

    // 3. E-STOP latches FAULT, RESET clears it
    Send(PumpCommand::START);
    WaitFor("START -> PRIMING (again)", 3s, [] { return StateIs(PumpState::PRIMING); });
    Send(PumpCommand::ESTOP);
    WaitFor("E-STOP -> FAULT(ESTOP_REMOTE)", 3s,
        [] { return StateIs(PumpState::FAULT) && g_fault == AlarmCode::ESTOP_REMOTE; });
    WaitFor("ESTOP_REMOTE alarm raised", 3s, [] { return AlarmIs(AlarmCode::ESTOP_REMOTE, true); });
    Send(PumpCommand::START);
    os::Thread::Sleep(500ms);
    WaitFor("START ignored while FAULT latched", 1s, [] { return StateIs(PumpState::FAULT); });
    WaitFor("pump coasted to stop", 5s, [] { return g_rpm < STOPPED_RPM; });
    Send(PumpCommand::RESET);
    WaitFor("RESET -> IDLE", 3s, [] { return StateIs(PumpState::IDLE); });
    WaitFor("ESTOP_REMOTE alarm cleared", 3s, [] { return AlarmIs(AlarmCode::ESTOP_REMOTE, false); });

    // 4. Losing the GUI heartbeat safe-stops a running pump
    Send(PumpCommand::SET_SPEED, 1000);
    Send(PumpCommand::START);
    WaitFor("pump RUNNING before link-loss test", 8s, [] { return StateIs(PumpState::RUNNING); });
    System::GetInstance().SetHeartbeatEnabled(false);
    WaitFor("GUI heartbeat loss -> STOPPING",
        std::chrono::duration_cast<std::chrono::milliseconds>(HEARTBEAT_TIMEOUT) + 3s,
        [] { return StateIs(PumpState::STOPPING) || StateIs(PumpState::IDLE); });
    WaitFor("GUI_LINK_LOST alarm raised", 3s, [] { return AlarmIs(AlarmCode::GUI_LINK_LOST, true); });
    System::GetInstance().SetHeartbeatEnabled(true);
    WaitFor("GUI_LINK_LOST alarm cleared on reconnect", 5s, [] { return AlarmIs(AlarmCode::GUI_LINK_LOST, false); });
    WaitFor("controller back to IDLE", 8s, [] { return StateIs(PumpState::IDLE); });

    // 5. Link health
    const auto& stats = System::GetInstance().GetLinkStats();
    printf("  INFO  link stats: acked=%u retries=%u failed=%u dropped=%u\n",
           stats.acked.load(), stats.retries.load(), stats.deliveryFailed.load(), stats.sendDropped.load());
    if (stats.acked == 0) {
        printf("  FAIL  no RELIABLE command was ACKed\n");
        g_failures++;
    }
    if (stats.deliveryFailed != 0) {
        printf("  FAIL  RELIABLE delivery failures reported\n");
        g_failures++;
    }

    printf("Self-test %s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}

} // namespace gui
} // namespace pumptron
