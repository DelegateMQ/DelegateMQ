#include "Alarms.h"
#include "Constants.h"
#include "messages/RunStatusMsg.h"
#include "extras/util/ThreadMonitor.h"
#include <iostream>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {
namespace util {

void Alarms::Initialize() {
    dmq::util::ThreadMonitor::Register(&m_thread);
    m_thread.CreateThread(WATCHDOG_TIMEOUT);
    m_ticksWaited = 0;

    // 1-second timer to track startup grace period
    if (!m_alarmGraceTimer) {
        m_alarmGraceTimer = std::make_unique<Timer>();
    }
    m_alarmGraceConn = m_alarmGraceTimer->OnExpired.Connect(dmq::util::MakeTimerDelegate(this, &Alarms::OnTimerExpired, m_thread));
    m_alarmGraceTimer->Start(std::chrono::milliseconds(1000));

    // 1. Subscribe to system faults
    m_faultConn = dmq::databus::DataBus::Subscribe<FaultMsg>(topics::FAULT, dmq::MakeDelegate(this, &Alarms::OnFault), &m_thread);

    // 2. Filtered subscription to run status. Only receive when the system is in FAULT.
    // This demonstrates the dmq::databus::DataBus::SubscribeFilter feature.
    m_runStatusConn = dmq::databus::DataBus::SubscribeFilter<RunStatusMsg>(
        topics::STATUS_RUN, 
        dmq::MakeDelegate(this, &Alarms::OnRunStatus), 
        dmq::MakeDelegate(this, &Alarms::OnRunStatusFilter),
        &m_thread);

    // 3. Setup Watchdog for safety heartbeat
    // Use explicit 'new' to ensure the XALLOCATOR overloaded operator new is called.
    // std::make_unique and dmq::xmake_shared bypass class-specific operator new.
    m_safetyWatchdog.reset(new dmq::databus::DeadlineSubscription<HeartbeatMsg>(
        topics::SAFETY_HEARTBEAT,
        HEARTBEAT_TIMEOUT,
        dmq::MakeDelegate(this, &Alarms::OnHeartbeatSuccess),
        dmq::MakeDelegate(this, &Alarms::OnSafetyWatchdogTimeout),
        &m_thread
    ));

    // 3. Setup Watchdog for controller heartbeat
    m_controllerWatchdog.reset(new dmq::databus::DeadlineSubscription<HeartbeatMsg>(
        topics::CONTROLLER_HEARTBEAT,
        HEARTBEAT_TIMEOUT,
        dmq::MakeDelegate(this, &Alarms::OnHeartbeatSuccess),
        dmq::MakeDelegate(this, &Alarms::OnControllerWatchdogTimeout),
        &m_thread
    ));
}

void Alarms::OnTimerExpired() {
    m_ticksWaited++;
}

void Alarms::OnFault(FaultMsg msg) {
    // NOTE: IsNewer() guard intentionally omitted for faults. Prioritize safety
    // over ordering; a "nuisance trip" from an old fault is safer than missing 
    // a trip due to a timestamp race.
    if (m_alarmActive && !CanOverrideActiveAlarm(msg.faultCode)) {
        return;
    }

    std::string message;
    switch (msg.faultCode) {
        case FAULT_OVERSPEED:       message = "ALARM: Centrifuge Overspeed"; break;
        case FAULT_SAFETY_LOST:     message = "ALARM: Safety Node Lost"; break;
        case FAULT_AIR_INLET:       message = "ALARM: Air Detected in Inlet"; break;
        case FAULT_BLOCKAGE:        message = "ALARM: Outlet Blockage Detected"; break;
        case FAULT_CONTROLLER_LOST: message = "ALARM: Controller Node Lost"; break;
        default:                    message = "ALARM: Unknown System Fault (" + std::to_string(msg.faultCode) + ")"; break;
    }
    SetAlarm(message, true);
}

void Alarms::OnRunStatus(RunStatusMsg msg) {
    if (m_statusGuard.IsNewer(msg.seq)) {
        SetAlarm("ALARM: System-wide Fault Detected", true);
    }
}

bool Alarms::OnRunStatusFilter(const RunStatusMsg& msg) {
    return msg.status == RunStatus::FAULT;
}

bool Alarms::CanOverrideActiveAlarm(uint32_t code) {
    switch (code) {
        case FAULT_OVERSPEED: return true;   // hardware safety — always surfaced
        default:              return false;  // operational faults do not displace active alarm
    }
}

void Alarms::OnSafetyWatchdogTimeout() {
    if (!m_alarmActive && m_ticksWaited >= HEARTBEAT_WARMUP.count()) {
        SetAlarm("ALARM: Safety Node Heartbeat Lost", true);
    }
}

void Alarms::OnControllerWatchdogTimeout() {
    if (!m_alarmActive && m_ticksWaited >= HEARTBEAT_WARMUP.count()) {
        SetAlarm("ALARM: Controller Node Heartbeat Lost", true);
    }
}

void Alarms::Shutdown() {
    // Disconnect before Stop() so any callback already queued on m_thread is dropped
    // when the thread processes it rather than firing after shutdown begins.
    m_alarmGraceConn.Disconnect();
    if (m_alarmGraceTimer) m_alarmGraceTimer->Stop();
    m_safetyWatchdog.reset();
    m_controllerWatchdog.reset();
    m_faultConn.Disconnect();
    m_runStatusConn.Disconnect();
    m_thread.ExitThread();
}

void Alarms::Reset() {
    (void)dmq::MakeDelegate(this, &Alarms::SetAlarm, m_thread).AsyncInvoke("No Alarm", false);
}

void Alarms::SetAlarm(const std::string& message, bool active) {
    m_currentMessage = message;
    m_alarmActive = active;
    
    // Emit signal to Notify UI (and anyone else)
    OnAlarmChanged(m_currentMessage, m_alarmActive);
}

} // namespace util
} // namespace cellutron
