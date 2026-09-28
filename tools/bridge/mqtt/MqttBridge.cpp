// MqttBridge.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// MQTT output for the JsonTopics layer (Paho MQTT C, synchronous client with
// callbacks).

#include "MqttBridge.h"
#include "MQTTClient.h"
#include <chrono>

namespace {
    constexpr const char* ONLINE_SUFFIX = "/online";
    constexpr const char* COMMAND_SUFFIX = "/set";
    constexpr char ONLINE_TRUE[] = "true";
    constexpr char ONLINE_FALSE[] = "false";
    constexpr int ONLINE_TRUE_LEN = sizeof(ONLINE_TRUE) - 1;
    constexpr int ONLINE_FALSE_LEN = sizeof(ONLINE_FALSE) - 1;
    constexpr int CONNECT_TIMEOUT_SEC = 3;
    constexpr size_t BRIDGE_QUEUE_SIZE = 500;
    /// Minimum time between "queue full" reports.
    constexpr int64_t DROP_REPORT_INTERVAL_MS = 5000;

    int64_t SteadyMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    int QosFor(const JsonTopics::Options& options) { return options.reliable ? 1 : 0; }
}

/// Paho callbacks. Run on Paho's internal thread.
struct MqttBridgePaho {
    static void ConnectionLost(void*, char* cause) {
        auto& inst = MqttBridge::GetInstance();
        if (inst.connected.exchange(false))
            MqttBridge::RaiseEvent(std::string("connection lost") + (cause ? std::string(": ") + cause : ""));
    }

    static int MessageArrived(void*, char* topicName, int topicLen, MQTTClient_message* message) {
        auto& inst = MqttBridge::GetInstance();
        std::string mqttTopic = topicLen > 0 ? std::string(topicName, static_cast<size_t>(topicLen)) : std::string(topicName);
        std::string payload(static_cast<const char*>(message->payload), static_cast<size_t>(message->payloadlen));
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);

        // "<prefix>/<topic>/set" -> "<topic>". Handled here, not queued to the
        // bridge thread: that queue carries outbound messages with
        // FullPolicy::DROP, and a command must never be dropped behind a
        // backlog. Parsing is quick and DataBus::Publish doesn't block.
        std::string prefix = MqttBridge::MqttTopic("");
        std::string suffix = COMMAND_SUFFIX;
        if (inst.running && mqttTopic.size() > prefix.size() + suffix.size()
            && mqttTopic.compare(0, prefix.size(), prefix) == 0
            && mqttTopic.compare(mqttTopic.size() - suffix.size(), suffix.size(), suffix) == 0) {
            std::string topic = mqttTopic.substr(prefix.size(), mqttTopic.size() - prefix.size() - suffix.size());
            JsonTopics::Inbound(topic, payload, "MQTT");
        }
        return 1;   // message consumed
    }
};

// ---------------------------------------------------------------------------
// JsonTopics sink: runs on the JsonTopics thread; only queues work.
// ---------------------------------------------------------------------------

void MqttBridge::Sink::OnMessage(const std::string& topic, const std::string& json, const JsonTopics::Options& options) {
    auto& inst = GetInstance();
    // While disconnected, don't queue: the message would be sent after
    // Connect()'s replay of current state, briefly re-publishing an older
    // value. Latched state is covered by that replay; the rest is dropped.
    if (!inst.running || !inst.thread || !inst.connected) return;
    (void)dmq::MakeDelegate(&MqttBridge::Send, *inst.thread).AsyncInvoke(
        MqttTopic(topic), json, options.latched, QosFor(options));
}

void MqttBridge::Sink::OnTopicAccepted(const std::string& topic) {
    auto& inst = GetInstance();
    if (!inst.running || !inst.thread) return;
    (void)dmq::MakeDelegate(&MqttBridge::SubscribeCommand, *inst.thread).AsyncInvoke(topic);
}

// ---------------------------------------------------------------------------

std::string MqttBridge::MqttTopic(const std::string& topic) {
    const auto& prefix = GetInstance().options.topicPrefix;
    return prefix.empty() ? topic : prefix + "/" + topic;
}

bool MqttBridge::IsConnected() {
    return GetInstance().connected;
}

void MqttBridge::RaiseEvent(const std::string& text) {
    JsonTopics::RaiseEvent("MQTT: " + text);
}

bool MqttBridge::Start(const Options& options) {
    auto& inst = GetInstance();
    if (inst.thread) return true;

    inst.options = options;
    if (inst.options.clientId.empty())
        inst.options.clientId = inst.options.topicPrefix + "-bridge";

    MQTTClient client = nullptr;
    int rc = MQTTClient_create(&client, inst.options.brokerUri.c_str(), inst.options.clientId.c_str(),
                               MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTCLIENT_SUCCESS) {
        RaiseEvent("cannot create MQTT client for " + inst.options.brokerUri + " (rc=" + std::to_string(rc) + ")");
        return false;
    }
    rc = MQTTClient_setCallbacks(client, nullptr, &MqttBridgePaho::ConnectionLost, &MqttBridgePaho::MessageArrived, nullptr);
    if (rc != MQTTCLIENT_SUCCESS) {
        MQTTClient_destroy(&client);
        RaiseEvent("cannot set MQTT callbacks (rc=" + std::to_string(rc) + ")");
        return false;
    }
    inst.client = client;

    // DROP: a slow or unreachable broker must never stall the JsonTopics layer.
    inst.thread = std::make_unique<dmq::os::Thread>("MqttBridge", BRIDGE_QUEUE_SIZE, dmq::os::FullPolicy::DROP);
    inst.thread->SetDroppedHandler(dmq::MakeDelegate(&MqttBridge::OnQueueFull));
    inst.thread->CreateThread();

    inst.running = true;
    JsonTopics::AddSink(&inst.sink);
    inst.supervisor = std::thread(&MqttBridge::SupervisorLoop);
    return true;
}

void MqttBridge::Stop() {
    auto& inst = GetInstance();
    if (!inst.thread) return;

    // No new messages, then no reconnects, then drain the bridge thread.
    JsonTopics::RemoveSink(&inst.sink);
    inst.running = false;
    if (inst.supervisor.joinable()) inst.supervisor.join();
    inst.thread->ExitThread();

    // Only this thread uses the client now (Paho's own thread aside).
    MQTTClient client = static_cast<MQTTClient>(inst.client);
    if (inst.connected.exchange(false)) {
        std::string topic = inst.options.topicPrefix + ONLINE_SUFFIX;
        MQTTClient_deliveryToken token = 0;
        if (MQTTClient_publish(client, topic.c_str(), ONLINE_FALSE_LEN, ONLINE_FALSE, 1, 1, &token) == MQTTCLIENT_SUCCESS)
            MQTTClient_waitForCompletion(client, token, 1000);
        MQTTClient_disconnect(client, 1000);
    }
    MQTTClient_destroy(&client);
    inst.client = nullptr;
    inst.thread.reset();
}

void MqttBridge::SupervisorLoop() {
    auto& inst = GetInstance();
    while (inst.running) {
        if (!inst.connected) {
            // Connect on the bridge thread, the only thread that sends. Not a
            // blocking call: the thread's DROP policy could discard it.
            (void)dmq::MakeDelegate(&MqttBridge::Connect, *inst.thread).AsyncInvoke();
        }
        // Sleep in short steps so Stop() isn't held up.
        for (int i = 0; i < inst.options.reconnectSec * 10 && inst.running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void MqttBridge::Connect() {
    auto& inst = GetInstance();
    if (inst.connected || !inst.running) return;
    MQTTClient client = static_cast<MQTTClient>(inst.client);

    std::string onlineTopic = inst.options.topicPrefix + ONLINE_SUFFIX;
    MQTTClient_willOptions will = MQTTClient_willOptions_initializer;
    will.topicName = onlineTopic.c_str();
    will.message = ONLINE_FALSE;
    will.retained = 1;
    will.qos = 1;

    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    opts.keepAliveInterval = inst.options.keepAliveSec;
    opts.cleansession = 1;
    opts.connectTimeout = CONNECT_TIMEOUT_SEC;
    opts.will = &will;

    static bool reportedUnreachable = false;
    int rc = MQTTClient_connect(client, &opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        // Report the first failure of a run of failures, not every retry.
        if (!reportedUnreachable) {
            RaiseEvent("cannot connect to " + inst.options.brokerUri + " (rc=" + std::to_string(rc)
                       + "); retrying every " + std::to_string(inst.options.reconnectSec) + " s");
            reportedUnreachable = true;
        }
        return;
    }
    reportedUnreachable = false;

    // Command topics (clean session: subscribe on every connect).
    auto accepted = JsonTopics::AcceptedTopics();
    for (const auto& topic : accepted) {
        std::string commandTopic = MqttTopic(topic) + COMMAND_SUFFIX;
        if (MQTTClient_subscribe(client, commandTopic.c_str(), 1) != MQTTCLIENT_SUCCESS)
            RaiseEvent("cannot subscribe to " + commandTopic);
    }

    MQTTClient_deliveryToken token = 0;
    MQTTClient_publish(client, onlineTopic.c_str(), ONLINE_TRUE_LEN, ONLINE_TRUE, 1, 1, &token);

    // Connected BEFORE the snapshot below: a message published from here on is
    // queued behind this function, so it's sent after the replay and the
    // newest value always goes out last.
    inst.connected = true;

    // Current state, including anything published before this connection.
    auto exposed = JsonTopics::ExposedTopics();
    for (const auto& [topic, json] : JsonTopics::LatchedValues()) {
        int qos = 1;
        for (const auto& [t, options] : exposed)
            if (t == topic) qos = QosFor(options);
        std::string mqttTopic = MqttTopic(topic);
        MQTTClient_publish(client, mqttTopic.c_str(), static_cast<int>(json.size()), json.data(), qos, 1, &token);
    }
    RaiseEvent("connected to " + inst.options.brokerUri + " as " + inst.options.clientId
               + (accepted.empty() ? "" : " (" + std::to_string(accepted.size()) + " command topic(s))"));
}

void MqttBridge::Send(std::string mqttTopic, std::string payload, bool retained, int qos) {
    auto& inst = GetInstance();
    if (!inst.connected)
        return;     // latched values are re-published by Connect()
    MQTTClient_deliveryToken token = 0;
    int rc = MQTTClient_publish(static_cast<MQTTClient>(inst.client), mqttTopic.c_str(),
                                static_cast<int>(payload.size()), payload.data(), qos, retained ? 1 : 0, &token);
    if (rc == MQTTCLIENT_DISCONNECTED && inst.connected.exchange(false))
        RaiseEvent("connection lost while publishing " + mqttTopic);
}

void MqttBridge::SubscribeCommand(std::string topic) {
    // Accepted after connecting; Connect() covers topics accepted earlier.
    auto& inst = GetInstance();
    if (!inst.connected) return;
    std::string commandTopic = MqttTopic(topic) + COMMAND_SUFFIX;
    if (MQTTClient_subscribe(static_cast<MQTTClient>(inst.client), commandTopic.c_str(), 1) != MQTTCLIENT_SUCCESS)
        RaiseEvent("cannot subscribe to " + commandTopic);
}

void MqttBridge::OnQueueFull(size_t depth) {
    // Runs synchronously on the JsonTopics thread: count, and report at most
    // once per interval.
    auto& inst = GetInstance();
    uint32_t count = ++inst.queueDrops;
    int64_t now = SteadyMs();
    int64_t last = inst.lastDropReportMs;
    if (now - last < DROP_REPORT_INTERVAL_MS || !inst.lastDropReportMs.compare_exchange_strong(last, now))
        return;
    inst.queueDrops -= count;
    RaiseEvent("queue full (" + std::to_string(depth) + " queued): dropped " + std::to_string(count)
               + " outbound message(s); broker slow or unreachable");
}
