#include "MqttGateway.h"
#include "JsonTopics.h"
#include "MqttBridge.h"
#include "BridgeJson.h"
#include "util/Constants.h"
#include "messages/PumpStatusMsg.h"
#include "messages/TelemetryMsg.h"
#include "messages/AlarmMsg.h"
#include "messages/PumpCommandMsg.h"
#include <cstring>

namespace pumptron {

namespace {

const char* const COMMAND_NAMES[] = { "START", "STOP", "ESTOP", "RESET", "SET_SPEED", "QUERY" };

// Stable identifiers for MQTT consumers to match on (automations, rules). The
// display strings from ToString(AlarmCode) are sent separately as "text".
const char* AlarmCodeName(AlarmCode c) {
    switch (c) {
        case AlarmCode::NONE:           return "NONE";
        case AlarmCode::TEMP_HIGH:      return "TEMP_HIGH";
        case AlarmCode::VIBRATION_HIGH: return "VIBRATION_HIGH";
        case AlarmCode::OVER_TEMP:      return "OVER_TEMP";
        case AlarmCode::VIBRATION_TRIP: return "VIBRATION_TRIP";
        case AlarmCode::ESTOP_REMOTE:   return "ESTOP_REMOTE";
        case AlarmCode::ESTOP_LOCAL:    return "ESTOP_LOCAL";
        case AlarmCode::GUI_LINK_LOST:  return "GUI_LINK_LOST";
        case AlarmCode::LINK_DEGRADED:  return "LINK_DEGRADED";
        case AlarmCode::COUNT:          break;
    }
    return "UNKNOWN";
}

std::string StatusToJson(const PumpStatusMsg& m) {
    return bridgejson::Writer()
        .Add("state", ToString(m.state))
        .Add("setpoint", static_cast<int>(m.setpointRpm))
        .Add("fault", AlarmCodeName(m.faultCode))
        .Str();
}

std::string TelemetryToJson(const TelemetryMsg& m) {
    return bridgejson::Writer()
        .Add("rpm", m.rpm, 0)
        .Add("flow", m.flowLpm, 2)
        .Add("pressure", m.pressureBar, 3)
        .Add("motorTemp", m.motorTempC, 1)
        .Add("ambientTemp", m.ambientTempC, 1)
        .Add("vibration", m.vibrationG, 3)
        .Str();
}

std::string AlarmToJson(const AlarmMsg& m) {
    return bridgejson::Writer()
        .Add("code", AlarmCodeName(m.code))
        .Add("text", ToString(m.code))
        .Add("severity", ToString(m.severity))
        .Add("active", m.active)
        .Str();
}

// Every accepted payload becomes a command to the pump: reject anything that
// isn't a known command with valid arguments.
bool CommandFromJson(const std::string& json, PumpCommandMsg& cmd) {
    bridgejson::Reader r;
    std::string name;
    if (!r.Parse(json) || !r.GetString("command", name)) return false;

    for (size_t i = 0; i < sizeof(COMMAND_NAMES) / sizeof(COMMAND_NAMES[0]); ++i) {
        if (name != COMMAND_NAMES[i]) continue;
        cmd.command = static_cast<PumpCommand>(i);
        if (cmd.command == PumpCommand::SET_SPEED) {
            double rpm = 0;
            // Same limits as the GUI's own setpoint control.
            if (!r.GetNumber("rpm", rpm) || rpm < MIN_SETPOINT_RPM || rpm > MAX_SETPOINT_RPM) return false;
            cmd.setpointRpm = static_cast<uint16_t>(rpm);
        }
        return true;
    }
    return false;
}

} // namespace

bool StartMqttGateway(const MqttGatewayOptions& options)
{
    // The pump's JSON view, declared once for every bridge (JsonTopics).
    JsonTopics::Expose<PumpStatusMsg>(topics::STATUS, &StatusToJson, { true /*latched*/, true /*reliable*/ });
    JsonTopics::Expose<TelemetryMsg>(topics::TELEMETRY, &TelemetryToJson);
    JsonTopics::Expose<AlarmMsg>(topics::ALARM, &AlarmToJson, { false, true /*reliable*/ });
    if (options.allowControl)
        JsonTopics::Accept<PumpCommandMsg>(topics::CMD, &CommandFromJson);

    MqttBridge::Options bridgeOptions;
    bridgeOptions.brokerUri = options.brokerUri;
    bridgeOptions.topicPrefix = options.topicPrefix;
    return MqttBridge::Start(bridgeOptions);
}

void StopMqttGateway()
{
    MqttBridge::Stop();
    JsonTopics::Shutdown();
}

} // namespace pumptron
