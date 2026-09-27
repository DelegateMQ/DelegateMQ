// MqttBridge.h
// @see https://github.com/DelegateMQ/DelegateMQ
// DataBus <-> MQTT gateway: exposes chosen DataBus topics as MQTT topics with
// JSON payloads, so MQTT tools (Node-RED, Home Assistant, Grafana, MQTT
// Explorer, cloud IoT) can read and optionally command a DelegateMQ system.
//
// Unlike port/transport/mqtt/MqttTransport (which tunnels DelegateMQ binary
// frames between DelegateMQ apps on one fixed MQTT topic), each DataBus topic
// gets its own MQTT topic and a readable payload:
//
//   DataBus "pump/telemetry"  ->  MQTT "<prefix>/pump/telemetry"  {"rpm":1500,...}
//   MQTT "<prefix>/pump/cmd/set" {"command":"START"}  ->  DataBus "pump/cmd"
//
// Inbound (MQTT -> DataBus) uses a separate "<topic>/set" MQTT topic, the
// usual MQTT command-topic convention, so a topic can be both published and
// commanded without echoing back into itself. Inbound is opt-in per topic:
// every Subscribe() is a remote control path into the application.
//
// The bridge also maintains "<prefix>/online": "true" (retained) while
// connected, and "false" via MQTT Last Will if the connection drops.
//
// Usage (after DataBus topics exist; call from one thread):
//   MqttBridge::Options opt;
//   opt.brokerUri = "tcp://127.0.0.1:1883";
//   opt.topicPrefix = "pumptron";
//   MqttBridge::Start(opt);
//   MqttBridge::Publish<TelemetryMsg>("pump/telemetry", &TelemetryToJson);
//   MqttBridge::Publish<PumpStatusMsg>("pump/status", &StatusToJson, MqttBridge::Retain::YES, 1);
//   MqttBridge::Subscribe<PumpCommandMsg>("pump/cmd", &CommandFromJson);
//   ...
//   MqttBridge::Stop();
//
// Threading: DataBus messages are converted and sent on the bridge's own
// thread (FullPolicy::DROP), so publishers never block on the network. If that
// queue fills (broker slow or unreachable), outbound messages are dropped and
// reported via OnEvent(), at most once every few seconds. The broker
// connection is retried in the background; messages published while
// disconnected are dropped. Inbound messages are parsed on Paho's receive
// thread and published on the DataBus directly, so a command is never dropped
// behind an outbound backlog. DataBus subscribers registered without a thread
// therefore run on Paho's thread; give command subscribers a thread.
//
// Build: tools/bridge/MqttBridge.cmake adds this bridge and Paho MQTT C to a
// target (dmq_add_mqtt_bridge). Desktop only (Paho).

#ifndef MQTT_BRIDGE_H
#define MQTT_BRIDGE_H

#include "DelegateMQ.h"
#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class MqttBridge {
public:
    enum class Retain { NO, YES };

    struct Options {
        std::string brokerUri = "tcp://127.0.0.1:1883";
        /// MQTT topic = topicPrefix + "/" + DataBus topic. Also names "<prefix>/online".
        std::string topicPrefix = "dmq";
        /// Must be unique per broker. Empty: "<topicPrefix>-bridge".
        std::string clientId;
        int keepAliveSec = 20;
        /// Delay between reconnect attempts while the broker is unreachable.
        int reconnectSec = 2;
    };

    /// @brief Start the bridge thread and begin connecting to the broker.
    /// @return false if the MQTT client couldn't be created (e.g. malformed URI).
    /// An unreachable broker is not an error: the bridge keeps retrying.
    static bool Start(const Options& options);

    /// @brief Publish "<prefix>/online" = "false", disconnect and stop the thread.
    static void Stop();

    static bool IsConnected();

    /// @brief Forward a DataBus topic to MQTT as "<prefix>/<topic>".
    /// @param toJson Converts each message to the MQTT payload (usually JSON).
    /// @param retain Retain the last value at the broker, so a newly connected
    /// dashboard sees the current state immediately (MQTT's Last Value Cache).
    /// The bridge also remembers the last retained value and re-publishes it on
    /// every (re)connect, so state published while disconnected isn't lost.
    /// @param qos MQTT QoS 0 or 1. Use 0 for high-rate telemetry.
    template <typename T>
    static void Publish(const std::string& topic, std::function<std::string(const T&)> toJson,
                        Retain retain = Retain::NO, int qos = 0)
    {
        auto& inst = GetInstance();
        if (!inst.thread) {
            RaiseEvent("Publish(" + topic + ") ignored: bridge not started");
            return;
        }
        std::string mqttTopic = MqttTopic(topic);
        bool retained = retain == Retain::YES;
        auto conn = dmq::databus::DataBus::Subscribe<T>(topic.c_str(),
            [toJson, mqttTopic, retained, qos](const T& msg) {
                SendPayload(mqttTopic, toJson(msg), retained, qos);
            }, inst.thread.get());
        std::lock_guard<std::mutex> lock(inst.mutex);
        inst.connections.push_back(std::move(conn));
    }

    /// @brief Accept commands from MQTT "<prefix>/<topic>/set" and publish them
    /// on the DataBus topic. Payloads that fromJson rejects are reported via
    /// OnEvent() and dropped.
    /// @param fromJson Parses a payload into T; returns false if invalid.
    template <typename T>
    static void Subscribe(const std::string& topic, std::function<bool(const std::string&, T&)> fromJson, int qos = 1)
    {
        auto& inst = GetInstance();
        if (!inst.thread) {
            RaiseEvent("Subscribe(" + topic + ") ignored: bridge not started");
            return;
        }
        std::string mqttTopic = MqttTopic(topic) + "/set";
        InboundHandler handler = [fromJson, topic](const std::string& payload) {
            T msg{};
            if (!fromJson(payload, msg)) {
                RaiseEvent("rejected command on " + topic + ": " + payload);
                return;
            }
            dmq::databus::DataBus::Publish<T>(topic.c_str(), msg);
        };
        AddInbound(mqttTopic, std::move(handler), qos);
    }

    /// @brief Connection changes and problems as readable text ("connected to
    /// tcp://...", "connection lost", "rejected command on ..."). Emitted from
    /// the bridge's threads. Without a subscriber, events go to stderr.
    static dmq::Signal<void(const std::string&)>& OnEvent();

    /// @brief "<prefix>/<dataBusTopic>"
    static std::string MqttTopic(const std::string& dataBusTopic);

private:
    MqttBridge() = default;

    using InboundHandler = std::function<void(const std::string& payload)>;

    struct Inbound {
        InboundHandler handler;
        int qos = 1;
    };

    struct Instance {
        Options options;
        std::unique_ptr<dmq::os::Thread> thread;    ///< Converts, sends, handles inbound
        std::thread supervisor;                     ///< Reconnects while disconnected
        void* client = nullptr;                     ///< MQTTClient (opaque here)
        std::atomic<bool> running{false};
        std::atomic<bool> connected{false};
        std::atomic<uint32_t> dropped{0};           ///< Sends skipped while disconnected
        std::atomic<uint32_t> queueDrops{0};        ///< Outbound messages dropped by a full queue, not yet reported
        std::atomic<int64_t> lastDropReportMs{0};   ///< Rate-limits queue-full reports
        std::mutex mutex;                           ///< Guards connections and inbound
        std::list<dmq::ScopedConnection> connections;
        std::map<std::string, Inbound> inbound;     ///< Keyed by MQTT topic
        std::map<std::string, std::pair<std::string, int>> retained;  ///< Last retained payload, QoS per MQTT topic (bridge thread only)
        dmq::Signal<void(const std::string&)> onEvent;
    };

    static Instance& GetInstance() {
        static Instance instance;
        return instance;
    }

    static void SendPayload(const std::string& mqttTopic, const std::string& payload, bool retained, int qos);
    static void AddInbound(const std::string& mqttTopic, InboundHandler handler, int qos);
    static void RaiseEvent(const std::string& text);

    static void SupervisorLoop();
    static void Connect();
    static void HandleInbound(std::string mqttTopic, std::string payload);
    static void OnQueueFull(size_t depth);

    // Paho callbacks, defined in MqttBridge.cpp (their signatures use Paho types).
    friend struct MqttBridgePaho;
};

#endif // MQTT_BRIDGE_H
