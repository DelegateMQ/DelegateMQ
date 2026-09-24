#include "PumpController.h"
#include "util/Constants.h"
#include "messages/TelemetryMsg.h"
#include <cstdio>

using namespace dmq;
using namespace dmq::databus;
using namespace pumptron::board;

namespace pumptron {
namespace pump {

PumpController::PumpController(IBoard& board)
    : m_board(board)
    , m_thread("Pump", dmq::DEFAULT_QUEUE_SIZE, dmq::os::FullPolicy::FAULT)
    , m_setpointRpm(DEFAULT_SETPOINT_RPM)
{
}

PumpController::~PumpController()
{
    Stop();
}

bool PumpController::Start(std::optional<dmq::Duration> watchdog)
{
    if (!m_thread.CreateThread(watchdog)) {
        printf("PumpController: ERROR - failed to create thread\n");
        return false;
    }

    // Board init touches peripherals; do it on the pump thread that will own them.
    (void)MakeDelegate(this, &PumpController::Init, m_thread).AsyncInvoke();
    return true;
}

void PumpController::Stop()
{
    m_controlTimer.Stop();
    m_heartbeatTimer.Stop();
    m_thread.ExitThread();
    m_controlConn.Disconnect();
    m_heartbeatConn.Disconnect();
    m_commandConn.Disconnect();
    m_guiWatch.reset();
}

void PumpController::Init()
{
    m_board.Init();
    m_model.Reset(m_board.ReadAmbientTempC());
    m_board.SetIndicator(Indicator::POWER, true);

    // Late local subscribers get the current state immediately.
    DataBus::LastValueCache(topics::STATUS, true);

    m_commandConn = DataBus::Subscribe<PumpCommandMsg>(
        topics::CMD, MakeDelegate(this, &PumpController::OnCommand), &m_thread);

    // GUI liveness: any heartbeat re-arms the deadline; silence for
    // HEARTBEAT_TIMEOUT fires OnGuiLinkLost() (repeatedly, until it resumes).
    m_guiWatch.emplace(
        topics::HB_GUI,
        HEARTBEAT_TIMEOUT,
        MakeDelegate(this, &PumpController::OnGuiHeartbeat),
        MakeDelegate(this, &PumpController::OnGuiLinkLost),
        &m_thread);

    m_lastTick = Clock::now();
    m_controlConn = m_controlTimer.OnExpired.Connect(
        util::MakeTimerDelegate(this, &PumpController::OnControlTick, m_thread));
    m_controlTimer.Start(CONTROL_PERIOD);

    m_heartbeatConn = m_heartbeatTimer.OnExpired.Connect(
        util::MakeTimerDelegate(this, &PumpController::OnHeartbeatTick, m_thread));
    m_heartbeatTimer.Start(HEARTBEAT_PERIOD);

    EnterState(PumpState::IDLE);
    printf("PumpController: ready on %s board\n", m_board.Name());
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void PumpController::OnCommand(const PumpCommandMsg& msg)
{
    switch (msg.command) {
    case PumpCommand::START:
        if (m_state == PumpState::IDLE) {
            printf("PumpController: START\n");
            EnterState(PumpState::PRIMING);
        }
        break;

    case PumpCommand::STOP:
        if (m_state == PumpState::PRIMING || m_state == PumpState::RUNNING) {
            printf("PumpController: STOP\n");
            EnterState(PumpState::STOPPING);
        }
        break;

    case PumpCommand::ESTOP:
        printf("PumpController: E-STOP (remote)\n");
        Trip(AlarmCode::ESTOP_REMOTE);
        break;

    case PumpCommand::RESET:
        TryReset();
        break;

    case PumpCommand::SET_SPEED: {
        uint16_t rpm = msg.setpointRpm;
        if (rpm < MIN_SETPOINT_RPM) rpm = MIN_SETPOINT_RPM;
        if (rpm > MAX_SETPOINT_RPM) rpm = MAX_SETPOINT_RPM;
        m_setpointRpm = rpm;
        PublishStatus();
        break;
    }

    case PumpCommand::QUERY:
        PublishStatus();
        RepublishActiveAlarms();
        break;
    }
}

// ---------------------------------------------------------------------------
// Control loop
// ---------------------------------------------------------------------------

void PumpController::OnControlTick()
{
    const TimePoint now = Clock::now();
    float dt = std::chrono::duration<float>(now - m_lastTick).count();
    m_lastTick = now;
    if (dt < 0.0f || dt > 0.25f) dt = 0.05f;   // guard against first tick / debugger halt

    const float ambientC = m_board.ReadAmbientTempC();
    const float extVibG = m_board.ReadVibrationG();

    // Local stop button (edge-triggered)
    const bool pressed = m_board.IsLocalStopPressed();
    if (pressed && !m_localStopWasPressed) {
        printf("PumpController: E-STOP (board button)\n");
        Trip(AlarmCode::ESTOP_LOCAL);
    }
    m_localStopWasPressed = pressed;

    m_model.Step(dt, TargetRpm(), RampRate(), ambientC, extVibG);

    switch (m_state) {
    case PumpState::PRIMING:
        if (now - m_stateEntered >= PRIME_TIME)
            EnterState(PumpState::RUNNING);
        break;
    case PumpState::STOPPING:
        if (m_model.Rpm() < STOPPED_RPM)
            EnterState(PumpState::IDLE);
        break;
    default:
        break;
    }

    CheckProtections(dt);


    if (++m_tickCount % TELEMETRY_DIVIDER == 0)
        PublishTelemetry(ambientC);
}

void PumpController::CheckProtections(float dtSec)
{
    const float tempC = m_model.MotorTempC();
    const float vibG = m_model.VibrationG();
    const bool turning = m_state == PumpState::PRIMING
                      || m_state == PumpState::RUNNING
                      || m_state == PumpState::STOPPING;

    // Over-temperature
    if (tempC > TEMP_TRIP_C)
        Trip(AlarmCode::OVER_TEMP);

    const size_t tempIdx = static_cast<size_t>(AlarmCode::TEMP_HIGH);
    if (!m_alarmActive[tempIdx] && tempC > TEMP_WARN_C)
        SetAlarm(AlarmCode::TEMP_HIGH, AlarmSeverity::WARNING, true);
    else if (m_alarmActive[tempIdx] && tempC < TEMP_WARN_C - 3.0f)
        SetAlarm(AlarmCode::TEMP_HIGH, AlarmSeverity::WARNING, false);

    // Vibration: warn any time, trip only while the pump is turning and
    // the level is sustained (a single knock should not fault the pump).
    const size_t vibIdx = static_cast<size_t>(AlarmCode::VIBRATION_HIGH);
    if (!m_alarmActive[vibIdx] && vibG > VIB_WARN_G)
        SetAlarm(AlarmCode::VIBRATION_HIGH, AlarmSeverity::WARNING, true);
    else if (m_alarmActive[vibIdx] && vibG < VIB_WARN_G * 0.85f)
        SetAlarm(AlarmCode::VIBRATION_HIGH, AlarmSeverity::WARNING, false);

    if (turning && vibG > VIB_TRIP_G)
        m_vibOverTripSec += dtSec;
    else
        m_vibOverTripSec = 0.0f;

    if (m_vibOverTripSec >= std::chrono::duration<float>(VIB_TRIP_TIME).count())
        Trip(AlarmCode::VIBRATION_TRIP);
}

float PumpController::TargetRpm() const
{
    switch (m_state) {
    case PumpState::PRIMING: return static_cast<float>(PRIME_RPM);
    case PumpState::RUNNING: return static_cast<float>(m_setpointRpm);
    default:                 return 0.0f;
    }
}

float PumpController::RampRate() const
{
    switch (m_state) {
    case PumpState::STOPPING: return RAMP_DOWN_RPM_PER_S;
    case PumpState::FAULT:    return COAST_RPM_PER_S;
    default:                  return RAMP_UP_RPM_PER_S;
    }
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

void PumpController::EnterState(PumpState state, AlarmCode fault)
{
    m_state = state;
    m_fault = fault;
    m_stateEntered = Clock::now();
    m_vibOverTripSec = 0.0f;

    m_board.SetIndicator(Indicator::RUNNING, state == PumpState::PRIMING || state == PumpState::RUNNING);
    m_board.SetIndicator(Indicator::FAULT, state == PumpState::FAULT);

    printf("PumpController: -> %s\n", ToString(state));
    PublishStatus();
}

void PumpController::Trip(AlarmCode code)
{
    if (m_state == PumpState::FAULT)
        return;     // first fault wins and stays latched

    SetAlarm(code, AlarmSeverity::FAULT, true);
    EnterState(PumpState::FAULT, code);
}

void PumpController::TryReset()
{
    if (m_state != PumpState::FAULT)
        return;

    const char* refusal = nullptr;
    if (m_model.Rpm() >= STOPPED_RPM)
        refusal = "pump still turning";
    else if (m_model.MotorTempC() >= TEMP_RESET_C)
        refusal = "motor still hot";
    else if (m_board.IsLocalStopPressed())
        refusal = "board stop button held";

    if (refusal) {
        printf("PumpController: RESET refused (%s)\n", refusal);
        PublishStatus();   // resync the GUI's view of the latched fault
        return;
    }

    printf("PumpController: RESET\n");
    SetAlarm(m_fault, AlarmSeverity::FAULT, false);
    EnterState(PumpState::IDLE);
}

// ---------------------------------------------------------------------------
// Link supervision
// ---------------------------------------------------------------------------

void PumpController::OnHeartbeatTick()
{
    DataBus::Publish<HeartbeatMsg>(topics::HB_CONTROLLER, HeartbeatMsg(++m_heartbeatCount));
}

void PumpController::OnGuiHeartbeat(const HeartbeatMsg&)
{
    if (!m_guiOnline) {
        m_guiOnline = true;
        printf("PumpController: GUI link up\n");
        SetAlarm(AlarmCode::GUI_LINK_LOST, AlarmSeverity::WARNING, false);
        // Bring a (re)connected GUI up to date without waiting for a change.
        PublishStatus();
        RepublishActiveAlarms();
    }
}

void PumpController::OnGuiLinkLost()
{
    // Fires every HEARTBEAT_TIMEOUT while the GUI stays silent; log the transition only.
    if (m_guiOnline)
        printf("PumpController: GUI link lost\n");
    m_guiOnline = false;

    // No operator watching -> never keep the pump running unattended.
    if (m_state == PumpState::PRIMING || m_state == PumpState::RUNNING) {
        SetAlarm(AlarmCode::GUI_LINK_LOST, AlarmSeverity::WARNING, true);
        EnterState(PumpState::STOPPING);
    }
}

// ---------------------------------------------------------------------------
// Publishing
// ---------------------------------------------------------------------------

void PumpController::PublishStatus()
{
    DataBus::Publish<PumpStatusMsg>(topics::STATUS, PumpStatusMsg(m_state, m_fault, m_setpointRpm));
}

void PumpController::PublishTelemetry(float ambientC)
{
    TelemetryMsg msg;
    msg.rpm = m_model.Rpm();
    msg.flowLpm = m_model.FlowLpm();
    msg.pressureBar = m_model.PressureBar();
    msg.motorTempC = m_model.MotorTempC();
    msg.ambientTempC = ambientC;
    msg.vibrationG = m_model.VibrationG();
    DataBus::Publish<TelemetryMsg>(topics::TELEMETRY, msg);

    m_board.ToggleIndicator(Indicator::ACTIVITY);
}

void PumpController::SetAlarm(AlarmCode code, AlarmSeverity severity, bool active)
{
    const size_t idx = static_cast<size_t>(code);
    if (code == AlarmCode::NONE || idx >= ALARM_COUNT || m_alarmActive[idx] == active)
        return;

    m_alarmActive[idx] = active;
    m_alarmSeverity[idx] = severity;
    printf("PumpController: alarm %s %s\n", ToString(code), active ? "RAISED" : "cleared");
    DataBus::Publish<AlarmMsg>(topics::ALARM, AlarmMsg(code, severity, active));
}

void PumpController::RepublishActiveAlarms()
{
    for (size_t i = 1; i < ALARM_COUNT; ++i) {
        if (m_alarmActive[i])
            DataBus::Publish<AlarmMsg>(topics::ALARM,
                AlarmMsg(static_cast<AlarmCode>(i), m_alarmSeverity[i], true));
    }
}

} // namespace pump
} // namespace pumptron
