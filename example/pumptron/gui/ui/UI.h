#ifndef PUMPTRON_UI_H
#define PUMPTRON_UI_H

#include "DelegateMQ.h"
#include "messages/PumpStatusMsg.h"
#include "messages/TelemetryMsg.h"
#include "messages/AlarmMsg.h"
#include "messages/HeartbeatMsg.h"
#include "messages/PumpCommandMsg.h"
#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <string>

namespace pumptron {
namespace gui {

/// @brief FTXUI operator console.
///
/// DataBus handlers run on the UI's own worker thread (FullPolicy::DROP -- a
/// burst of telemetry must never block the link) and only update a
/// mutex-protected snapshot; FTXUI's render loop reads that snapshot.
class UI {
public:
    static UI& GetInstance() {
        static UI instance;
        return instance;
    }

    /// Run the UI event loop (blocks until the operator quits).
    /// @param linkDescription Shown in the header.
    void Run(const std::string& linkDescription);

    void Shutdown();

private:
    UI() = default;
    ~UI();

    UI(const UI&) = delete;
    UI& operator=(const UI&) = delete;

    // --- DataBus handlers (UI worker thread) ---
    void OnStatus(const PumpStatusMsg& msg);
    void OnTelemetry(const TelemetryMsg& msg);
    void OnAlarm(const AlarmMsg& msg);
    void OnControllerHeartbeat(const HeartbeatMsg& msg);
    void OnControllerLost();
    void OnBusMonitor(const dmq::databus::SpyPacket& packet);
    void OnMessageDropped(size_t depth);

    void SendCommand(PumpCommand command, uint16_t setpointRpm = 0);
    void AddEvent(const std::string& text);
    void Refresh();

    static constexpr size_t HISTORY = 120;      ///< 12 s of telemetry at 10 Hz
    static constexpr size_t MAX_EVENTS = 50;
    static constexpr size_t MAX_BUS_LINES = 50;

    // --- Snapshot (guarded by m_mutex) ---
    dmq::Mutex m_mutex;
    bool m_online = false;
    bool m_haveStatus = false;
    PumpStatusMsg m_status;
    TelemetryMsg m_telemetry;
    std::deque<float> m_rpmHistory;
    std::deque<float> m_tempHistory;
    std::deque<float> m_vibHistory;
    std::array<bool, static_cast<size_t>(AlarmCode::GUI_LINK_LOST) + 1> m_alarmActive{};
    std::array<AlarmSeverity, static_cast<size_t>(AlarmCode::GUI_LINK_LOST) + 1> m_alarmSeverity{};
    std::deque<std::string> m_events;
    std::deque<std::string> m_busLines;
    uint32_t m_telemetryCount = 0;
    uint32_t m_heartbeatsSeen = 0;
    bool m_waitLogged = false;

    // --- Setpoint slider (FTXUI thread only) ---
    int m_sliderRpm = 0;
    int m_lastSentRpm = 0;
    dmq::TimePoint m_lastSetpointSend{};

    dmq::os::Thread m_thread{ "UI", dmq::DEFAULT_QUEUE_SIZE, dmq::os::FullPolicy::DROP };

    dmq::ScopedConnection m_statusConn;
    dmq::ScopedConnection m_telemetryConn;
    dmq::ScopedConnection m_alarmConn;
    dmq::ScopedConnection m_monitorConn;
    std::unique_ptr<dmq::databus::DeadlineSubscription<HeartbeatMsg>> m_controllerWatch;
};

} // namespace gui
} // namespace pumptron

#endif
