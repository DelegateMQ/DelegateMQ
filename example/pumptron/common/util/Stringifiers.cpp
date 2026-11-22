#include "Stringifiers.h"
#include "Constants.h"
#include "messages/PumpCommandMsg.h"
#include "messages/PumpStatusMsg.h"
#include "messages/TelemetryMsg.h"
#include "messages/AlarmMsg.h"
#include "messages/HeartbeatMsg.h"

#include <cstdio>

using namespace dmq;
using namespace dmq::databus;

namespace pumptron {

namespace {

xstring StringifyCommand(const PumpCommandMsg& m)
{
    static const char* const names[] = { "START", "STOP", "ESTOP", "RESET", "SET_SPEED", "QUERY" };
    const auto i = static_cast<size_t>(m.command);
    const char* name = i < sizeof(names) / sizeof(names[0]) ? names[i] : "?";
    char buf[48];
    if (m.command == PumpCommand::SET_SPEED)
        snprintf(buf, sizeof(buf), "%s %u RPM", name, static_cast<unsigned>(m.setpointRpm));
    else
        snprintf(buf, sizeof(buf), "%s", name);
    return buf;
}

xstring StringifyStatus(const PumpStatusMsg& m)
{
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "%s sp=%u", ToString(m.state), static_cast<unsigned>(m.setpointRpm));
    if (m.faultCode != AlarmCode::NONE && n > 0 && static_cast<size_t>(n) < sizeof(buf))
        snprintf(buf + n, sizeof(buf) - n, " fault=%s", ToString(m.faultCode));
    return buf;
}

// "name=value unit" so tools that extract numbers from the text (dmq-spy
// --plotjuggler) get one named series per field.
xstring StringifyTelemetry(const TelemetryMsg& m)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "rpm=%.0f  flow=%.1f L/min  pressure=%.2f bar  temp=%.1f C  vib=%.2f g",
             m.rpm, m.flowLpm, m.pressureBar, m.motorTempC, m.vibrationG);
    return buf;
}

xstring StringifyAlarm(const AlarmMsg& m)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s %s %s", ToString(m.severity), ToString(m.code),
             m.active ? "RAISED" : "cleared");
    return buf;
}

xstring StringifyHeartbeat(const HeartbeatMsg& m)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "#%lu", static_cast<unsigned long>(m.counter));
    return buf;
}

} // namespace

void RegisterStringifiers()
{
    DataBus::RegisterStringifier<PumpCommandMsg>(topics::CMD, MakeDelegate(&StringifyCommand));
    DataBus::RegisterStringifier<PumpStatusMsg>(topics::STATUS, MakeDelegate(&StringifyStatus));
    DataBus::RegisterStringifier<TelemetryMsg>(topics::TELEMETRY, MakeDelegate(&StringifyTelemetry));
    DataBus::RegisterStringifier<AlarmMsg>(topics::ALARM, MakeDelegate(&StringifyAlarm));
    DataBus::RegisterStringifier<HeartbeatMsg>(topics::HB_CONTROLLER, MakeDelegate(&StringifyHeartbeat));
    DataBus::RegisterStringifier<HeartbeatMsg>(topics::HB_GUI, MakeDelegate(&StringifyHeartbeat));
}

} // namespace pumptron
