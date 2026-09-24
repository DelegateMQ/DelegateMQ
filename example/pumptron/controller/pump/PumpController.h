#ifndef PUMPTRON_PUMP_CONTROLLER_H
#define PUMPTRON_PUMP_CONTROLLER_H

#include "DelegateMQ.h"
#include "board/IBoard.h"
#include "pump/PumpModel.h"
#include "messages/PumpCommandMsg.h"
#include "messages/PumpStatusMsg.h"
#include "messages/AlarmMsg.h"
#include "messages/HeartbeatMsg.h"
#include <array>
#include <atomic>
#include <optional>

namespace pumptron {
namespace pump {

/// @brief Pump controller active object.
///
/// Owns one thread. Every handler -- operator commands, the control-loop tick,
/// heartbeat publish and the GUI-link deadline -- is dispatched onto that
/// thread by DelegateMQ, so the state machine needs no locks.
///
/// The controller talks to the outside world only through the DataBus:
/// - Subscribes: topics::CMD, topics::HB_GUI (deadline-watched)
/// - Publishes:  topics::STATUS, topics::TELEMETRY, topics::ALARM,
///               topics::HB_CONTROLLER
/// It has no idea whether those topics reach a GUI over RS-232, UDP, or not
/// at all -- that is decided by the platform main() (see Topology.h).
///
/// State machine:
/// @code
///   IDLE --START--> PRIMING --(PRIME_TIME)--> RUNNING
///   PRIMING/RUNNING --STOP or GUI link lost--> STOPPING --(stopped)--> IDLE
///   any --ESTOP / button / over-temp / vibration trip--> FAULT (latched)
///   FAULT --RESET (stopped, cooled, button released)--> IDLE
/// @endcode
class PumpController {
public:
    explicit PumpController(board::IBoard& board);
    ~PumpController();

    PumpController(const PumpController&) = delete;
    PumpController& operator=(const PumpController&) = delete;

    /// Exposed so an RTOS target can set priority/stack before Start().
    dmq::os::Thread& GetThread() { return m_thread; }

    /// Create the thread, subscribe to the DataBus and start the control loop.
    bool Start(std::optional<dmq::Duration> watchdog = std::nullopt);

    void Stop();

    /// @brief Report a link or DataBus error (delivery failure, dropped frame,
    ///        retry backlog, serialization error).
    /// @details Thread-safe; callable from any thread, e.g. link and DataBus
    ///          signal handlers. Raises the LINK_DEGRADED warning (cleared after
    ///          LINK_DEGRADED_HOLD without errors) and blinks the POWER LED.
    ///          Error storms are coalesced into one pending dispatch.
    void ReportLinkError(const char* what);

    /// @brief Republish current status and active alarms, rate-limited to
    ///        RESYNC_MIN_INTERVAL. Call when a RELIABLE status/alarm message
    ///        was abandoned, so the GUI doesn't keep showing a stale state.
    ///        Thread-safe; coalesced like ReportLinkError().
    void RequestResync();

private:
    // --- Dispatched handlers (all run on m_thread) ---
    void Init();
    void OnCommand(const PumpCommandMsg& msg);
    void OnControlTick();
    void OnHeartbeatTick();
    void OnGuiHeartbeat(const HeartbeatMsg& msg);
    void OnGuiLinkLost();
    void HandleLinkError();
    void HandleResync();

    // --- State machine helpers ---
    void EnterState(PumpState state, AlarmCode fault = AlarmCode::NONE);
    void Trip(AlarmCode code);
    void TryReset();
    void CheckProtections(float dtSec);
    float TargetRpm() const;
    float RampRate() const;

    // --- Publishing helpers ---
    void PublishStatus();
    void PublishTelemetry(float ambientC);
    void SetAlarm(AlarmCode code, AlarmSeverity severity, bool active);
    void RepublishActiveAlarms();

    board::IBoard& m_board;
    PumpModel m_model;
    dmq::os::Thread m_thread;

    PumpState m_state = PumpState::IDLE;
    AlarmCode m_fault = AlarmCode::NONE;
    uint16_t  m_setpointRpm;
    dmq::TimePoint m_stateEntered{};
    dmq::TimePoint m_lastTick{};
    float     m_vibOverTripSec = 0.0f;
    bool      m_localStopWasPressed = false;
    bool      m_guiOnline = false;
    uint32_t  m_tickCount = 0;
    uint32_t  m_heartbeatCount = 0;
    dmq::TimePoint m_lastLinkError{};
    dmq::TimePoint m_lastResync{};

    // Cross-thread error reporting: set by any thread, consumed on m_thread.
    std::atomic<bool> m_ready{false};           ///< Init() finished on m_thread
    std::atomic<bool> m_linkErrorPending{false};
    std::atomic<bool> m_resyncPending{false};

    static constexpr size_t ALARM_COUNT = ALARM_CODE_COUNT;
    std::array<bool, ALARM_COUNT> m_alarmActive{};
    std::array<AlarmSeverity, ALARM_COUNT> m_alarmSeverity{};

    dmq::util::Timer      m_controlTimer;
    dmq::util::Timer      m_heartbeatTimer;
    dmq::ScopedConnection m_controlConn;
    dmq::ScopedConnection m_heartbeatConn;
    dmq::ScopedConnection m_commandConn;

    // Constructed in place on m_thread (non-movable, no heap needed).
    std::optional<dmq::databus::DeadlineSubscription<HeartbeatMsg>> m_guiWatch;
};

} // namespace pump
} // namespace pumptron

#endif
