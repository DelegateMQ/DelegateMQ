// MqttBridge.h
// @see https://github.com/DelegateMQ/DelegateMQ
// MQTT output for the JsonTopics layer (../common/JsonTopics.h): serves every
// topic the application exposes there as its own MQTT topic with a JSON
// payload, so any MQTT client can read and, for accepted topics, command a
// DelegateMQ system. No tool-specific integration: dashboard tools consume it
// through their own MQTT support (see tools/TOOLS.md, "Using MQTT Tools").
//
// Unlike port/transport/mqtt/MqttTransport (which tunnels DelegateMQ binary
// frames between DelegateMQ apps on one fixed MQTT topic), each topic gets its
// own MQTT topic and a readable JSON payload:
//
//   JsonTopics::Expose "pump/telemetry"  ->  MQTT "<prefix>/pump/telemetry"
//   MQTT "<prefix>/pump/cmd/set"         ->  JsonTopics::Accept "pump/cmd"
//
// Mapping of JsonTopics hints: latched -> retained message (also re-published
// on every (re)connect); reliable -> QoS 1, otherwise QoS 0. Inbound uses a
// separate "<topic>/set" MQTT topic, the usual command-topic convention, so a
// topic can be both exposed and accepted without echoing back into itself.
//
// The bridge also maintains "<prefix>/online": "true" (retained) while
// connected, and "false" via MQTT Last Will if the connection drops.
//
// Usage (topics can be exposed before or after Start()):
//   JsonTopics::Expose<TelemetryMsg>("pump/telemetry", &TelemetryToJson);
//   MqttBridge::Options opt;
//   opt.brokerUri = "tcp://127.0.0.1:1883";
//   opt.topicPrefix = "pumptron";
//   MqttBridge::Start(opt);
//   ...
//   MqttBridge::Stop();
//
// Threading: MQTT sends run on the bridge's own thread (FullPolicy::DROP), so
// a slow broker never blocks the JsonTopics layer or other bridges. If that
// queue fills, messages are dropped and reported via JsonTopics::OnEvent(), at
// most once every few seconds. The broker connection is retried in the
// background; messages published while disconnected are dropped (latched ones
// are re-published on connect). Inbound commands are handed to
// JsonTopics::Inbound() on Paho's receive thread, so a command is never
// dropped behind an outbound backlog.
//
// Events (connects, disconnects, rejected commands) go to JsonTopics::OnEvent(),
// prefixed "MQTT: ".
//
// Build: MqttBridge.cmake (dmq_add_mqtt_bridge) adds this bridge, the shared
// layer and Paho MQTT C to a target. Desktop only (Paho).

#ifndef MQTT_BRIDGE_H
#define MQTT_BRIDGE_H

#include "JsonTopics.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

class MqttBridge {
public:
    struct Options {
        std::string brokerUri = "tcp://127.0.0.1:1883";
        /// MQTT topic = topicPrefix + "/" + topic. Also names "<prefix>/online".
        std::string topicPrefix = "dmq";
        /// Must be unique per broker. Empty: "<topicPrefix>-bridge".
        std::string clientId;
        int keepAliveSec = 20;
        /// Delay between reconnect attempts while the broker is unreachable.
        int reconnectSec = 2;
    };

    /// @brief Start serving the JsonTopics layer over MQTT and begin connecting.
    /// @return false if the MQTT client couldn't be created (e.g. malformed URI).
    /// An unreachable broker is not an error: the bridge keeps retrying.
    static bool Start(const Options& options);

    /// @brief Publish "<prefix>/online" = "false", disconnect and stop the thread.
    static void Stop();

    static bool IsConnected();

    /// @brief "<prefix>/<topic>"
    static std::string MqttTopic(const std::string& topic);

private:
    MqttBridge() = default;

    /// JsonTopics sink; forwards to the bridge thread.
    class Sink : public JsonTopics::ISink {
    public:
        void OnMessage(const std::string& topic, const std::string& json, const JsonTopics::Options& options) override;
        void OnTopicAccepted(const std::string& topic) override;
    };

    struct Instance {
        Options options;
        Sink sink;
        std::unique_ptr<dmq::os::Thread> thread;    ///< Sends; connects
        std::thread supervisor;                     ///< Reconnects while disconnected
        void* client = nullptr;                     ///< MQTTClient (opaque here)
        std::atomic<bool> running{false};
        std::atomic<bool> connected{false};
        std::atomic<uint32_t> queueDrops{0};        ///< Dropped by a full queue, not yet reported
        std::atomic<int64_t> lastDropReportMs{0};
    };

    static Instance& GetInstance() {
        static Instance instance;
        return instance;
    }

    static void Send(std::string mqttTopic, std::string payload, bool retained, int qos);
    static void SubscribeCommand(std::string topic);
    static void SupervisorLoop();
    static void Connect();
    static void OnQueueFull(size_t depth);
    static void RaiseEvent(const std::string& text);

    // Paho callbacks, defined in MqttBridge.cpp (their signatures use Paho types).
    friend struct MqttBridgePaho;
};

#endif // MQTT_BRIDGE_H
