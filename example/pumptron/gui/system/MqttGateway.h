#ifndef PUMPTRON_MQTT_GATEWAY_H
#define PUMPTRON_MQTT_GATEWAY_H

#include <string>

namespace pumptron {

/// @brief Exposes the pump over MQTT: declares its JSON view with JsonTopics
/// (tools/bridge/common) and serves it with MqttBridge (tools/bridge/mqtt).
///
/// The GUI re-publishes everything it receives from the controller on its own
/// DataBus, so the gateway works the same over serial or the simulator link:
///
///   <prefix>/pump/status     {"state":"RUNNING","setpoint":1500,"fault":"NONE"}   retained
///   <prefix>/pump/telemetry  {"rpm":1500,"flow":2.3,"pressure":1.20,...}
///   <prefix>/pump/alarm      {"code":"OVER_TEMP","severity":"FAULT","active":true}
///   <prefix>/online          true / false (Last Will)
///
/// With allowControl, commands are accepted on <prefix>/pump/cmd/set, e.g.
///   {"command":"START"}  {"command":"SET_SPEED","rpm":1200}  {"command":"ESTOP"}
/// and sent to the controller exactly like the GUI's own buttons.
struct MqttGatewayOptions {
    std::string brokerUri;
    std::string topicPrefix = "pumptron";
    bool allowControl = false;
};

/// @return false if the MQTT client couldn't be created. An unreachable
/// broker is not an error; the bridge keeps retrying.
bool StartMqttGateway(const MqttGatewayOptions& options);
void StopMqttGateway();

} // namespace pumptron

#endif
