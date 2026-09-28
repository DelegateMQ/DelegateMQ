// JsonTopics.h
// @see https://github.com/DelegateMQ/DelegateMQ
// Shared exposure layer for JSON bridges: an application declares ONCE which
// DataBus topics to expose (and optionally accept commands on), with JSON
// converters. Any number of bridges (sinks) then serve that same set, e.g.
// MqttBridge today, a WebSocket/Foxglove bridge later:
//
//   JsonTopics::Expose<TelemetryMsg>("pump/telemetry", &TelemetryToJson);
//   JsonTopics::Expose<PumpStatusMsg>("pump/status", &StatusToJson, { true /*latched*/, true /*reliable*/ });
//   JsonTopics::Accept<PumpCommandMsg>("pump/cmd", &CommandFromJson);   // opt-in inbound
//   MqttBridge::Start(mqttOptions);                                      // serves all of the above
//   ...
//   MqttBridge::Stop();
//   JsonTopics::Shutdown();
//
// Each message is converted to JSON once, on the JsonTopics thread, no matter
// how many bridges are running, then handed to every sink. Sinks do their own
// (possibly slow) network I/O on their own threads, so one slow output can't
// hold up another.
//
// Per-topic hints are transport-neutral; each bridge maps them:
//   latched  - state, not events: keep the last value for late joiners
//              (MQTT: retained message; Foxglove: last value sent on subscribe).
//   reliable - prefer confirmed delivery (MQTT: QoS 1).
//
// Inbound: Accept<T>() registers a command topic; bridges deliver incoming
// payloads through Inbound(), which parses them with fromJson and publishes
// valid ones on the DataBus. Every Accept() is a remote control path into the
// application: validate everything in fromJson.
//
// Threading: DataBus deliveries are converted on the JsonTopics thread
// (FullPolicy::DROP, so publishers never block). Inbound() runs on the calling
// bridge's receive thread. OnEvent() is emitted from whichever thread raises it.
//
// Desktop/tools code (see CLAUDE.md "Port Exception"): uses std:: containers.

#ifndef JSON_TOPICS_H
#define JSON_TOPICS_H

#include "DelegateMQ.h"
#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class JsonTopics {
public:
    /// Transport-neutral delivery hints for an exposed topic.
    struct Options {
        bool latched = false;   ///< State: keep the last value for late joiners
        bool reliable = false;  ///< Prefer confirmed delivery
    };

    /// @brief A bridge serving the exposed topics (MQTT, WebSocket, ...).
    /// Callbacks run on the JsonTopics thread; keep them quick (queue the
    /// actual network I/O to the bridge's own thread).
    class ISink {
    public:
        virtual ~ISink() = default;
        /// A new JSON message on an exposed topic.
        virtual void OnMessage(const std::string& topic, const std::string& json, const Options& options) = 0;
        /// A topic was exposed after this sink was added (e.g. advertise it).
        virtual void OnTopicExposed(const std::string& /*topic*/, const Options& /*options*/) {}
        /// A command topic was accepted after this sink was added (e.g. subscribe to it).
        virtual void OnTopicAccepted(const std::string& /*topic*/) {}
    };

    /// @brief Expose a DataBus topic as JSON to every bridge.
    /// @param toJson Converts each message to its JSON text.
    template <typename T>
    static void Expose(const std::string& topic, std::function<std::string(const T&)> toJson, Options options = {})
    {
        auto& inst = GetInstance();
        EnsureThread();
        auto conn = dmq::databus::DataBus::Subscribe<T>(topic.c_str(),
            [toJson, topic, options](const T& msg) { Deliver(topic, toJson(msg), options); },
            inst.thread.get());
        std::vector<ISink*> sinks;
        {
            std::lock_guard<std::mutex> lock(inst.mutex);
            inst.connections.push_back(std::move(conn));
            inst.exposed[topic] = options;
            sinks = inst.sinks;
        }
        for (auto* sink : sinks)
            sink->OnTopicExposed(topic, options);
    }

    /// @brief Accept commands for a DataBus topic from every bridge.
    /// @param fromJson Parses a payload into T; return false to reject it.
    template <typename T>
    static void Accept(const std::string& topic, std::function<bool(const std::string&, T&)> fromJson)
    {
        auto& inst = GetInstance();
        InboundHandler handler = [fromJson, topic](const std::string& payload) {
            T msg{};
            if (!fromJson(payload, msg))
                return false;
            dmq::databus::DataBus::Publish<T>(topic.c_str(), msg);
            return true;
        };
        std::vector<ISink*> sinks;
        {
            std::lock_guard<std::mutex> lock(inst.mutex);
            inst.accepted[topic] = std::move(handler);
            sinks = inst.sinks;
        }
        for (auto* sink : sinks)
            sink->OnTopicAccepted(topic);
    }

    // ---- For bridges -------------------------------------------------------

    static void AddSink(ISink* sink);
    static void RemoveSink(ISink* sink);

    /// Exposed topics and their options, for a bridge that starts after them.
    static std::vector<std::pair<std::string, Options>> ExposedTopics();
    /// Accepted command topics.
    static std::vector<std::string> AcceptedTopics();
    /// Last JSON of every latched topic (for late joiners / reconnects).
    static std::vector<std::pair<std::string, std::string>> LatchedValues();

    /// @brief Deliver an incoming command payload for an accepted topic.
    /// @param source Bridge name for the rejection report, e.g. "MQTT".
    /// @return false if the topic isn't accepted or fromJson rejected the payload
    /// (reported via OnEvent()).
    static bool Inbound(const std::string& topic, const std::string& payload, const char* source);

    // ---- For applications and bridges --------------------------------------

    /// @brief Events from the layer and every bridge: connects, disconnects,
    /// rejected commands, dropped messages. Without a subscriber they go to stderr.
    static dmq::Signal<void(const std::string&)>& OnEvent();
    static void RaiseEvent(const std::string& text);

    /// @brief Unsubscribe from the DataBus, stop the thread, clear all
    /// registrations. Stop every bridge first.
    static void Shutdown();

private:
    JsonTopics() = default;

    using InboundHandler = std::function<bool(const std::string& payload)>;

    struct Instance {
        std::unique_ptr<dmq::os::Thread> thread;
        std::mutex mutex;                                   ///< Guards everything below
        std::list<dmq::ScopedConnection> connections;
        std::map<std::string, Options> exposed;
        std::map<std::string, InboundHandler> accepted;
        std::map<std::string, std::string> latched;         ///< Last JSON per latched topic
        std::vector<ISink*> sinks;
        dmq::Signal<void(const std::string&)> onEvent;
        std::atomic<uint32_t> queueDrops{0};                ///< Dropped deliveries, not yet reported
        std::atomic<int64_t> lastDropReportMs{0};
    };

    static Instance& GetInstance() {
        static Instance instance;
        return instance;
    }

    static void EnsureThread();
    static void Deliver(const std::string& topic, const std::string& json, const Options& options);
    static void OnQueueFull(size_t depth);
};

#endif // JSON_TOPICS_H
