#include "UI.h"
#include "messages/CentrifugeStatusMsg.h"
#include "messages/CentrifugeSpeedMsg.h"
#include "messages/RunStatusMsg.h"
#include "messages/StartProcessMsg.h"
#include "messages/StopProcessMsg.h"
#include "messages/FaultMsg.h"
#include "messages/ActuatorStatusMsg.h"
#include "messages/HeartbeatMsg.h"
#include "Constants.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <atomic>
#include <string>
#include <vector>
#include <iostream>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace ftxui;

namespace cellutron {
namespace gui {

UI::~UI() {
    Shutdown();
}

void UI::AddLog(const std::string& msg) {
    LockGuard<dmq::Mutex> lock(m_uiMutex);
    m_logs.push_back(msg);
    if (m_logs.size() > 50) {
        m_logs.erase(m_logs.begin());
    }
}

void UI::Start() {
    // 1. Start the DelegateMQ worker thread with Watchdog
    m_thread.CreateThread(WATCHDOG_TIMEOUT);

    // 2. Setup DataBus Subscriptions
    auto statusConn = dmq::databus::DataBus::Subscribe<CentrifugeStatusMsg>(topics::STATUS_CENTRIFUGE, [this](CentrifugeStatusMsg msg) {
        auto* screen = ScreenInteractive::Active();
        if (screen) screen->PostEvent(Event::Custom);
    }, &m_thread);

    auto cmdConn = dmq::databus::DataBus::Subscribe<CentrifugeSpeedMsg>(topics::RPM, [this](CentrifugeSpeedMsg msg) {
        m_currentRpm = msg.rpm;
        auto* screen = ScreenInteractive::Active();
        if (screen) screen->PostEvent(Event::Custom);
    }, &m_thread);

    auto runConn = dmq::databus::DataBus::Subscribe<RunStatusMsg>(topics::STATUS_RUN, [this](RunStatusMsg msg) {
        std::string status_text;
        switch (msg.status) {
            case RunStatus::IDLE: status_text = "IDLE"; break;
            case RunStatus::PROCESSING: status_text = "PROCESSING"; break;
            case RunStatus::ABORTING: status_text = "ABORTING"; break;
            case RunStatus::FAULT: status_text = "FAULT"; break;
        }
        AddLog("Status Changed: " + status_text);
        m_runStatus = msg.status;
        auto* screen = ScreenInteractive::Active();
        if (screen) screen->PostEvent(Event::Custom);
    }, &m_thread);

    auto pumpConn = dmq::databus::DataBus::Subscribe<ActuatorStatusMsg>(topics::STATUS_ACTUATOR, [this](ActuatorStatusMsg msg) {
        if (msg.type == ActuatorType::PUMP && msg.id == 1) {
            m_currentPumpSpeed = msg.value;
            auto* screen = ScreenInteractive::Active();
            if (screen) screen->PostEvent(Event::Custom);
        }
    }, &m_thread);

    auto faultConn = dmq::databus::DataBus::Subscribe<FaultMsg>(topics::FAULT, [this](FaultMsg msg) {
        AddLog(">>> CRITICAL FAULT RECEIVED <<<");
        m_runStatus = RunStatus::FAULT;
        m_currentRpm = 0;
        m_currentPumpSpeed = 0;
        auto* screen = ScreenInteractive::Active();
        if (screen) screen->PostEvent(Event::Custom);
    }, &m_thread);

    // 3. Controller Presence Watchdog: Monitor periodic heartbeat for offline detection
    m_controllerWatchdog = std::make_unique<dmq::databus::DeadlineSubscription<HeartbeatMsg>>(
        topics::CONTROLLER_HEARTBEAT,
        HEARTBEAT_TIMEOUT,
        [this](const HeartbeatMsg&) {
            // Heartbeat received -> Controller is online
            if (m_isOffline.load()) {
                m_isOffline = false;
                auto* screen = ScreenInteractive::Active();
                if (screen) screen->PostEvent(Event::Custom);
            }
        },
        [this]() {
            // No heartbeat for HEARTBEAT_TIMEOUT -> Controller is offline
            m_isOffline = true;
            m_currentRpm = 0;
            m_currentPumpSpeed = 0;
            auto* screen = ScreenInteractive::Active();
            if (screen) screen->PostEvent(Event::Custom);
        },
        &m_thread
    );

    // 4. Build FTXUI Components
    std::string btnLabel = " START ";

    auto btnControl = Button(&btnLabel, [this] {
        auto now = dmq::Clock::now();
        if (std::chrono::duration_cast<dmq::Duration>(now - m_lastClickTime).count() < 500) {
            return;
        }
        m_lastClickTime = now;

        RunStatus current = m_runStatus.load();
        if (current == RunStatus::IDLE) {
            AddLog("Command: START Process");
            dmq::databus::DataBus::Publish<StartProcessMsg>(topics::CMD_RUN, {});
        } else if (current == RunStatus::PROCESSING) {
            AddLog("Command: ABORT Process");
            dmq::databus::DataBus::Publish<StopProcessMsg>(topics::CMD_ABORT, {});
        }
    });

    auto component = Container::Vertical({ btnControl });

    auto renderer = Renderer(component, [&] {
        LockGuard<dmq::Mutex> lock(m_uiMutex);
        
        uint16_t rpm = m_currentRpm.load();
        float rpm_fraction = static_cast<float>(rpm) / static_cast<float>(MAX_CENTRIFUGE_RPM);

        int16_t pumpSpeed = m_currentPumpSpeed.load();
        float pump_fraction = static_cast<float>(std::abs(pumpSpeed)) / 100.0f;
        std::string pump_dir = (pumpSpeed < 0) ? " (REV)" : (pumpSpeed > 0 ? " (FWD)" : "");

        std::string status_text;
        Color status_color = Color::White;

        bool isOffline = m_isOffline.load();
        RunStatus currentStatus = m_runStatus.load();

        if (isOffline) {
            status_text = "OFFLINE";
            status_color = Color::Yellow;
            btnLabel = " DISCONN";
        } else {
            switch (currentStatus) {
                case RunStatus::IDLE: status_text = "IDLE"; status_color = Color::GrayDark; break;
                case RunStatus::PROCESSING: status_text = "PROCESSING"; status_color = Color::Green; break;
                case RunStatus::ABORTING: status_text = "ABORTING"; status_color = Color::Red; break;
                case RunStatus::FAULT: status_text = "FAULT"; status_color = Color::Red; break;
            }

            if (currentStatus == RunStatus::PROCESSING) btnLabel = " STOP  ";
            else if (currentStatus == RunStatus::ABORTING) btnLabel = " STOP  ";
            else if (currentStatus == RunStatus::FAULT) btnLabel = "LOCKED ";
            else btnLabel = " START ";
        }

        auto rpm_gauge = gauge(rpm_fraction) | vscroll_indicator | frame | size(HEIGHT, EQUAL, 3) | color(isOffline ? Color::GrayDark : Color::Blue);
        auto pump_gauge = gauge(pump_fraction) | vscroll_indicator | frame | size(HEIGHT, EQUAL, 3) | color(isOffline ? Color::GrayDark : Color::Green);
        
        Elements log_elements;
        int start_idx = std::max(0, (int)m_logs.size() - 6);
        for (size_t i = start_idx; i < m_logs.size(); ++i) {
            log_elements.push_back(text(m_logs[i]));
        }
        auto log_box = vbox(std::move(log_elements)) | size(HEIGHT, EQUAL, 6) | frame | border;

        return vbox({
            // TOP SECTION
            vbox({
                text("Cellutron GUI") | bold | center,
                separator(),
                hbox({
                    vbox({
                        text("Centrifuge RPM") | center,
                        rpm_gauge,
                        text(isOffline ? "--- RPM" : (std::to_string(rpm) + " RPM")) | center | bold,
                        separator(),
                        text("Pump Speed") | center,
                        pump_gauge,
                        text(isOffline ? "--- %" : (std::to_string(pumpSpeed) + "%" + pump_dir)) | center | bold,
                    }) | flex,
                    separator(),
                    vbox({
                        text("Control") | center,
                        btnControl->Render() | center,
                        filler(),
                        hbox({
                            text(" Status: "),
                            text(status_text) | color(status_color) | bold,
                        }) | center,
                    }) | flex,
                }),
            }),
            // FILLER to push logs to bottom
            filler(),
            // BOTTOM SECTION
            vbox({
                text("System Logs") | dim,
                log_box,
            }),
        }) | border;
    });

    // 5. Run Screen (blocks)
    try {
        auto screen = ScreenInteractive::TerminalOutput();
        screen.Loop(renderer);
    } catch (const std::exception&) {
    }

    // 6. Cleanup after UI exit
    Shutdown();
}

void UI::Shutdown() {
    m_controllerWatchdog.reset();
    m_thread.ExitThread();
}

} // namespace gui
} // namespace cellutron
