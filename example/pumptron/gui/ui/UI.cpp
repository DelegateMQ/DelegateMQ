#include "UI.h"
#include "util/Constants.h"
#include "system/System.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

using namespace dmq;
using namespace dmq::databus;
using namespace ftxui;

namespace pumptron {
namespace gui {

namespace {

// ---------------------------------------------------------------------------
// Stringifiers: give the Bus Monitor pane readable values for each topic.
// ---------------------------------------------------------------------------
xstring StringifyCommand(const PumpCommandMsg& m) {
    static const char* names[] = { "START", "STOP", "ESTOP", "RESET", "SET_SPEED", "QUERY" };
    const auto i = static_cast<size_t>(m.command);
    xostringstream oss;
    oss << (i < 6 ? names[i] : "?");
    if (m.command == PumpCommand::SET_SPEED) oss << " " << m.setpointRpm << " RPM";
    return oss.str();
}

xstring StringifyStatus(const PumpStatusMsg& m) {
    xostringstream oss;
    oss << ToString(m.state) << " sp=" << m.setpointRpm;
    if (m.faultCode != AlarmCode::NONE) oss << " fault=" << ToString(m.faultCode);
    return oss.str();
}

xstring StringifyTelemetry(const TelemetryMsg& m) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%.0f rpm  %.1f L/min  %.2f bar  %.1f C  %.2f g",
             m.rpm, m.flowLpm, m.pressureBar, m.motorTempC, m.vibrationG);
    return buf;
}

xstring StringifyAlarm(const AlarmMsg& m) {
    xostringstream oss;
    oss << ToString(m.severity) << " " << ToString(m.code) << (m.active ? " RAISED" : " cleared");
    return oss.str();
}

xstring StringifyHeartbeat(const HeartbeatMsg& m) {
    xostringstream oss;
    oss << "#" << m.counter;
    return oss.str();
}

void RegisterStringifiers() {
    DataBus::RegisterStringifier<PumpCommandMsg>(topics::CMD, MakeDelegate(&StringifyCommand));
    DataBus::RegisterStringifier<PumpStatusMsg>(topics::STATUS, MakeDelegate(&StringifyStatus));
    DataBus::RegisterStringifier<TelemetryMsg>(topics::TELEMETRY, MakeDelegate(&StringifyTelemetry));
    DataBus::RegisterStringifier<AlarmMsg>(topics::ALARM, MakeDelegate(&StringifyAlarm));
    DataBus::RegisterStringifier<HeartbeatMsg>(topics::HB_CONTROLLER, MakeDelegate(&StringifyHeartbeat));
    DataBus::RegisterStringifier<HeartbeatMsg>(topics::HB_GUI, MakeDelegate(&StringifyHeartbeat));
}

std::string Timestamp() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

std::string Fixed(float v, int decimals) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

Color StateColor(PumpState s) {
    switch (s) {
        case PumpState::IDLE:     return Color::GrayLight;
        case PumpState::PRIMING:  return Color::Yellow;
        case PumpState::RUNNING:  return Color::Green;
        case PumpState::STOPPING: return Color::Yellow;
        case PumpState::FAULT:    return Color::Red;
    }
    return Color::White;
}

/// Sparkline of `history`, scaled to [0, maxValue].
Element Sparkline(const std::deque<float>& history, float maxValue, Color lineColor) {
    std::vector<float> data(history.begin(), history.end());
    return graph([data, maxValue](int width, int height) {
        std::vector<int> out(static_cast<size_t>(width), 0);
        const int n = static_cast<int>(data.size());
        const int offset = std::max(0, n - width);    // show the newest `width` samples
        for (int x = 0; x < width && offset + x < n; ++x) {
            const float v = data[static_cast<size_t>(offset + x)];
            const float r = std::clamp(v / maxValue, 0.0f, 1.0f);
            out[static_cast<size_t>(x)] = static_cast<int>(r * static_cast<float>(height - 1));
        }
        return out;
    }) | color(lineColor);
}

void Push(std::deque<float>& d, float v, size_t max) {
    d.push_back(v);
    while (d.size() > max) d.pop_front();
}

} // namespace

UI::~UI()
{
    Shutdown();
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void UI::SendCommand(PumpCommand command, uint16_t setpointRpm)
{
    DataBus::Publish<PumpCommandMsg>(topics::CMD, PumpCommandMsg(command, setpointRpm));
}

// ---------------------------------------------------------------------------
// DataBus handlers (UI worker thread)
// ---------------------------------------------------------------------------

void UI::OnStatus(const PumpStatusMsg& msg)
{
    {
        LockGuard<Mutex> lock(m_mutex);
        const bool changed = !m_haveStatus || m_status.state != msg.state;
        m_status = msg;
        m_haveStatus = true;
        if (changed) {
            std::string e = std::string("State -> ") + ToString(msg.state);
            if (msg.state == PumpState::FAULT)
                e += std::string(" (") + ToString(msg.faultCode) + ")";
            m_events.push_back(Timestamp() + "  " + e);
            while (m_events.size() > MAX_EVENTS) m_events.pop_front();
        }
    }
    Refresh();
}

void UI::OnTelemetry(const TelemetryMsg& msg)
{
    {
        LockGuard<Mutex> lock(m_mutex);
        m_telemetry = msg;
        m_telemetryCount++;
        Push(m_rpmHistory, msg.rpm, HISTORY);
        Push(m_tempHistory, msg.motorTempC, HISTORY);
        Push(m_vibHistory, msg.vibrationG, HISTORY);
    }
    Refresh();
}

void UI::OnAlarm(const AlarmMsg& msg)
{
    const size_t idx = static_cast<size_t>(msg.code);
    {
        LockGuard<Mutex> lock(m_mutex);
        if (idx < m_alarmActive.size()) {
            if (m_alarmActive[idx] == msg.active)
                return;     // resync repeat (QUERY / reconnect) -- nothing new
            m_alarmActive[idx] = msg.active;
            m_alarmSeverity[idx] = msg.severity;
        }
    }
    AddEvent(std::string(msg.active ? "ALARM " : "clear ") + ToString(msg.severity) + "  " + ToString(msg.code));
    Refresh();
}

void UI::OnControllerHeartbeat(const HeartbeatMsg&)
{
    bool cameOnline = false;
    {
        LockGuard<Mutex> lock(m_mutex);
        m_heartbeatsSeen++;
        if (!m_online) {
            m_online = true;
            cameOnline = true;
        }
    }
    if (cameOnline) {
        AddEvent("Controller link UP");
        SendCommand(PumpCommand::QUERY);    // resync state + active alarms
        Refresh();
    }
}

void UI::OnControllerLost()
{
    // Fires every HEARTBEAT_TIMEOUT of silence; logged once per transition.
    bool wasOnline = false;
    bool firstWait = false;
    {
        LockGuard<Mutex> lock(m_mutex);
        wasOnline = m_online;
        firstWait = !m_online && m_heartbeatsSeen == 0 && !m_waitLogged;
        if (firstWait) m_waitLogged = true;
        m_online = false;
    }
    if (wasOnline)
        AddEvent("Controller link LOST (no heartbeat)");
    else if (firstWait)
        AddEvent("Waiting for controller heartbeat...");
    Refresh();
}

void UI::OnBusMonitor(const SpyPacket& packet)
{
    // Telemetry arrives at 10 Hz; sample it so control traffic stays visible.
    static uint32_t telemetrySeen = 0;
    if (packet.topic == topics::TELEMETRY && (telemetrySeen++ % 10) != 0)
        return;

    LockGuard<Mutex> lock(m_mutex);
    char line[256];
    snprintf(line, sizeof(line), "%-26s %s", packet.topic.c_str(), packet.value.c_str());
    m_busLines.push_back(line);
    while (m_busLines.size() > MAX_BUS_LINES) m_busLines.pop_front();
}

void UI::OnMessageDropped(size_t depth)
{
    // Runs on the publishing thread; AddEvent is mutex-protected.
    AddEvent("UI queue full, update dropped (depth " + std::to_string(depth) + ")");
}

void UI::AddEvent(const std::string& text)
{
    LockGuard<Mutex> lock(m_mutex);
    m_events.push_back(Timestamp() + "  " + text);
    while (m_events.size() > MAX_EVENTS) m_events.pop_front();
}

void UI::Refresh()
{
    if (auto* screen = ScreenInteractive::Active())
        screen->PostEvent(Event::Custom);
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

void UI::Run(const std::string& linkDescription)
{
    RegisterStringifiers();

    m_thread.SetDroppedHandler(MakeDelegate(this, &UI::OnMessageDropped));
    m_thread.CreateThread(WATCHDOG_TIMEOUT);

    m_statusConn = DataBus::Subscribe<PumpStatusMsg>(topics::STATUS, MakeDelegate(this, &UI::OnStatus), &m_thread);
    m_telemetryConn = DataBus::Subscribe<TelemetryMsg>(topics::TELEMETRY, MakeDelegate(this, &UI::OnTelemetry), &m_thread);
    m_alarmConn = DataBus::Subscribe<AlarmMsg>(topics::ALARM, MakeDelegate(this, &UI::OnAlarm), &m_thread);
    m_monitorConn = DataBus::Monitor(MakeDelegate(this, &UI::OnBusMonitor), &m_thread);

    m_controllerWatch.reset(new DeadlineSubscription<HeartbeatMsg>(
        topics::HB_CONTROLLER,
        HEARTBEAT_TIMEOUT,
        MakeDelegate(this, &UI::OnControllerHeartbeat),
        MakeDelegate(this, &UI::OnControllerLost),
        &m_thread));

    m_sliderRpm = m_lastSentRpm = DEFAULT_SETPOINT_RPM;
    AddEvent("Pumptron GUI started on " + linkDescription);

    auto screen = ScreenInteractive::Fullscreen();

    auto btnStart = Button(" START ", [this] { SendCommand(PumpCommand::START); }, ButtonOption::Ascii());
    auto btnStop  = Button(" STOP ",  [this] { SendCommand(PumpCommand::STOP); },  ButtonOption::Ascii());
    auto btnEstop = Button(" E-STOP ", [this] { SendCommand(PumpCommand::ESTOP); }, ButtonOption::Ascii());
    auto btnReset = Button(" RESET ", [this] { SendCommand(PumpCommand::RESET); }, ButtonOption::Ascii());
    auto slider = Slider("", &m_sliderRpm,
                         static_cast<int>(MIN_SETPOINT_RPM), static_cast<int>(MAX_SETPOINT_RPM),
                         static_cast<int>(SETPOINT_STEP_RPM));

    auto controls = Container::Horizontal({ btnStart, btnStop, btnEstop, btnReset, slider });

    auto renderer = Renderer(controls, [&, this] {
        // Setpoint changes are sent at most every 200 ms while dragging, and
        // always once the slider settles, so the serial link isn't flooded.
        if (m_sliderRpm != m_lastSentRpm &&
            Clock::now() - m_lastSetpointSend >= std::chrono::milliseconds(200)) {
            m_lastSentRpm = m_sliderRpm;
            m_lastSetpointSend = Clock::now();
            SendCommand(PumpCommand::SET_SPEED, static_cast<uint16_t>(m_sliderRpm));
        }

        const auto& stats = System::GetInstance().GetLinkStats();

        LockGuard<Mutex> lock(m_mutex);

        // --- Header ---
        auto linkState = m_online
            ? text(" ONLINE ") | bold | color(Color::Black) | bgcolor(Color::Green)
            : text(" OFFLINE ") | bold | color(Color::Black) | bgcolor(Color::Red);
        auto header = hbox({
            text(" PUMPTRON ") | bold | color(Color::Cyan),
            text(linkDescription) | dim,
            filler(),
            text("ack " + std::to_string(stats.acked.load()) +
                 "  retry " + std::to_string(stats.retries.load()) +
                 "  fail " + std::to_string(stats.deliveryFailed.load()) +
                 "  drop " + std::to_string(stats.sendDropped.load()) + "  ") | dim,
            linkState,
        });

        // --- State + controls ---
        const PumpState state = m_status.state;
        auto stateText = (m_online && m_haveStatus)
            ? text(" " + std::string(ToString(state)) + " ") | bold | color(StateColor(state)) | inverted
            : text(" --- ") | dim;
        Element faultText = text("");
        if (m_online && state == PumpState::FAULT)
            faultText = text("  " + std::string(ToString(m_status.faultCode))) | color(Color::Red) | bold;

        auto controlRow = hbox({
            text(" State: "), stateText, faultText,
            filler(),
            btnStart->Render(), text(" "), btnStop->Render(), text(" "),
            btnEstop->Render() | color(Color::Red), text(" "), btnReset->Render(),
        });

        auto setpointRow = hbox({
            text(" Setpoint "),
            slider->Render() | flex,
            text(" " + std::to_string(m_sliderRpm) + " RPM") | bold,
            text("   controller: " + (m_haveStatus ? std::to_string(m_status.setpointRpm) : std::string("-"))) | dim,
            text(" "),
        });

        // --- Telemetry ---
        const TelemetryMsg& t = m_telemetry;
        const bool live = m_online && m_telemetryCount > 0;
        auto value = [live](const std::string& s) { return live ? text(s) | bold : text("---") | dim; };
        auto row = [](const char* label, Element v, Element extra) {
            return hbox({ text(label) | size(WIDTH, EQUAL, 12), v | size(WIDTH, EQUAL, 12), extra | flex });
        };
        const Color tempColor = t.motorTempC > TEMP_TRIP_C ? Color::Red
                              : t.motorTempC > TEMP_WARN_C ? Color::Yellow : Color::Green;
        const Color vibColor = t.vibrationG > VIB_TRIP_G ? Color::Red
                             : t.vibrationG > VIB_WARN_G ? Color::Yellow : Color::Green;

        auto telemetry = vbox({
            row(" Speed",     value(Fixed(t.rpm, 0) + " RPM"),
                gauge(live ? t.rpm / MAX_SETPOINT_RPM : 0.0f) | color(Color::Blue)),
            row(" Flow",      value(Fixed(t.flowLpm, 1) + " L/min"), text("")),
            row(" Pressure",  value(Fixed(t.pressureBar, 2) + " bar"), text("")),
            row(" Motor",     value(Fixed(t.motorTempC, 1) + " C"),
                gauge(live ? t.motorTempC / 100.0f : 0.0f) | color(tempColor)),
            row(" Ambient",   value(Fixed(t.ambientTempC, 1) + " C"), text("")),
            row(" Vibration", value(Fixed(t.vibrationG, 2) + " g"),
                gauge(live ? t.vibrationG / (VIB_TRIP_G * 1.25f) : 0.0f) | color(vibColor)),
            separator(),
            hbox({ text(" RPM  ") | dim, Sparkline(m_rpmHistory, MAX_SETPOINT_RPM, Color::Blue) | flex }) | size(HEIGHT, EQUAL, 3),
            hbox({ text(" Temp ") | dim, Sparkline(m_tempHistory, 100.0f, tempColor) | flex }) | size(HEIGHT, EQUAL, 3),
            hbox({ text(" Vib  ") | dim, Sparkline(m_vibHistory, VIB_TRIP_G * 1.25f, vibColor) | flex }) | size(HEIGHT, EQUAL, 3),
        });

        // --- Alarms ---
        Elements alarmLines;
        for (size_t i = 1; i < m_alarmActive.size(); ++i) {
            if (!m_alarmActive[i]) continue;
            const bool isFault = m_alarmSeverity[i] == AlarmSeverity::FAULT;
            alarmLines.push_back(hbox({
                text(isFault ? " FAULT " : " WARN  ") | bold | color(isFault ? Color::Red : Color::Yellow),
                text(ToString(static_cast<AlarmCode>(i))),
            }));
        }
        if (alarmLines.empty())
            alarmLines.push_back(text(" none") | dim);

        // --- Events ---
        Elements eventLines;
        for (const auto& e : m_events) eventLines.push_back(text(" " + e));

        // --- Bus monitor ---
        Elements busLines;
        for (const auto& l : m_busLines) busLines.push_back(text(" " + l) | dim);

        return vbox({
            header,
            separator(),
            controlRow,
            setpointRow,
            separator(),
            hbox({
                window(text(" Telemetry "), telemetry) | size(WIDTH, GREATER_THAN, 50) | flex,
                vbox({
                    window(text(" Active Alarms "), vbox(std::move(alarmLines))),
                    window(text(" Events "), vbox(std::move(eventLines)) | focusPositionRelative(0, 1) | yframe | flex) | flex,
                }) | flex,
            }) | flex,
            window(text(" Bus Monitor (telemetry sampled 1 Hz) "),
                   vbox(std::move(busLines)) | focusPositionRelative(0, 1) | yframe) | size(HEIGHT, EQUAL, 8),
            text(" [s]tart  s[t]op  [e]-stop  [r]eset  [+/-] setpoint  [q]uit   |   Tab/arrows + Enter also work") | dim,
        });
    });

    auto app = CatchEvent(renderer, [&, this](Event event) {
        if (event == Event::Character('q') || event == Event::Escape) { screen.Exit(); return true; }
        if (event == Event::Character('s')) { SendCommand(PumpCommand::START); return true; }
        if (event == Event::Character('t')) { SendCommand(PumpCommand::STOP); return true; }
        if (event == Event::Character('e')) { SendCommand(PumpCommand::ESTOP); return true; }
        if (event == Event::Character('r')) { SendCommand(PumpCommand::RESET); return true; }
        if (event == Event::Character('+') || event == Event::Character('=')) {
            m_sliderRpm = std::min<int>(m_sliderRpm + SETPOINT_STEP_RPM, MAX_SETPOINT_RPM);
            return true;
        }
        if (event == Event::Character('-')) {
            m_sliderRpm = std::max<int>(m_sliderRpm - SETPOINT_STEP_RPM, MIN_SETPOINT_RPM);
            return true;
        }
        return false;
    });

    screen.Loop(app);
    Shutdown();
}

void UI::Shutdown()
{
    m_controllerWatch.reset();
    m_statusConn.Disconnect();
    m_telemetryConn.Disconnect();
    m_alarmConn.Disconnect();
    m_monitorConn.Disconnect();
    m_thread.ExitThread();
}

} // namespace gui
} // namespace pumptron
