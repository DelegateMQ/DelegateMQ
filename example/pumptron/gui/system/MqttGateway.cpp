#include "MqttGateway.h"
#include "MqttBridge.h"
#include "MqttJson.h"
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
    return mqttjson::Writer()
        .Add("state", ToString(m.state))
        .Add("setpoint", static_cast<int>(m.setpointRpm))
        .Add("fault", AlarmCodeName(m.faultCode))
        .Str();
}

std::string TelemetryToJson(const TelemetryMsg& m) {
    return mqttjson::Writer()
        .Add("rpm", m.rpm, 0)
        .Add("flow", m.flowLpm, 2)
        .Add("pressure", m.pressureBar, 3)
        .Add("motorTemp", m.motorTempC, 1)
        .Add("ambientTemp", m.ambientTempC, 1)
        .Add("vibration", m.vibrationG, 3)
        .Str();
}

std::string AlarmToJson(const AlarmMsg& m) {
    return mqttjson::Writer()
        .Add("code", AlarmCodeName(m.code))
        .Add("text", ToString(m.code))
        .Add("severity", ToString(m.severity))
        .Add("active", m.active)
        .Str();
}

// Every accepted payload becomes a command to the pump: reject anything that
// isn't a known command with valid arguments.
bool CommandFromJson(const std::string& json, PumpCommandMsg& cmd) {
    mqttjson::Reader r;
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
    MqttBridge::Options bridgeOptions;
    bridgeOptions.brokerUri = options.brokerUri;
    bridgeOptions.topicPrefix = options.topicPrefix;
    if (!MqttBridge::Start(bridgeOptions))
        return false;

    MqttBridge::Publish<PumpStatusMsg>(topics::STATUS, &StatusToJson, MqttBridge::Retain::YES, 1);
    MqttBridge::Publish<TelemetryMsg>(topics::TELEMETRY, &TelemetryToJson);
    MqttBridge::Publish<AlarmMsg>(topics::ALARM, &AlarmToJson, MqttBridge::Retain::NO, 1);
    if (options.allowControl)
        MqttBridge::Subscribe<PumpCommandMsg>(topics::CMD, &CommandFromJson);
    return true;
}

void StopMqttGateway()
{
    MqttBridge::Stop();
}

} // namespace pumptron
