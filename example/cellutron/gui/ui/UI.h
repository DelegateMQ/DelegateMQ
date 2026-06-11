#ifndef _UI_H
#define _UI_H

#include "DelegateMQ.h"
#include "messages/RunStatusMsg.h"
#include "messages/ActuatorStatusMsg.h"
#include "messages/HeartbeatMsg.h"
#include "Constants.h"
#include <atomic>
#include <string>
#include <vector>
#include <chrono>

namespace cellutron {

struct CentrifugeStatusMsg;
struct CentrifugeSpeedMsg;
struct FaultMsg;

namespace gui {

/// @brief Singleton class for the User Interface.
class UI {
public:
    static UI& GetInstance() {
        static UI instance;
        return instance;
    }

    /// Start the UI event loop (blocking).
    void Start();

    /// Shutdown the UI.
    void Shutdown();

private:
    UI() = default;
    ~UI();

    UI(const UI&) = delete;
    UI& operator=(const UI&) = delete;

    void AddLog(const std::string& msg);

    void OnCentrifugeStatus(CentrifugeStatusMsg msg);
    void OnRpm(CentrifugeSpeedMsg msg);
    void OnRunStatus(RunStatusMsg msg);
    void OnActuatorStatus(ActuatorStatusMsg msg);
    void OnFault(FaultMsg msg);
    void OnHeartbeat(const HeartbeatMsg& msg);
    void OnControllerTimeout();

    // --- State Members ---
    std::atomic<uint16_t> m_currentRpm{0};
    std::atomic<int16_t> m_currentPumpSpeed{0};
    std::atomic<RunStatus> m_runStatus{RunStatus::IDLE};
    std::atomic<bool> m_isOffline{false};

    dmq::Mutex m_uiMutex;
    std::vector<std::string> m_logs;
    dmq::TimePoint m_lastClickTime = dmq::Clock::now();

    // Use standardized thread name for Active Object subsystem
    dmq::os::Thread m_thread{"UIThread", dmq::DEFAULT_QUEUE_SIZE, dmq::os::FullPolicy::DROP};

    std::unique_ptr<dmq::databus::DeadlineSubscription<HeartbeatMsg>> m_controllerWatchdog;
};

} // namespace gui
} // namespace cellutron

#endif
