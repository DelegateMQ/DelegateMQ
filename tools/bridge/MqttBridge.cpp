// MqttBridge.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// DataBus <-> MQTT gateway implementation (Paho MQTT C, synchronous client
// with callbacks).

#include "MqttBridge.h"
#include "MQTTClient.h"
#include <chrono>
#include <iostream>
#include <vector>

namespace {
    constexpr const char* ONLINE_SUFFIX = "/online";
    constexpr int CONNECT_TIMEOUT_SEC = 3;
    constexpr size_t BRIDGE_QUEUE_SIZE = 500;
}

/// Paho callbacks. Run on Paho's internal thread; they only hand work to the
/// bridge thread.
struct MqttBridgePaho {
    static void ConnectionLost(void*, char* cause) {
        auto& inst = MqttBridge::GetInstance();
        if (inst.connected.exchange(false))
            MqttBridge::RaiseEvent(std::string("connection lost") + (cause ? std::string(": ") + cause : ""));
    }

    static int MessageArrived(void*, char* topicName, int topicLen, MQTTClient_message* message) {
        auto& inst = MqttBridge::GetInstance();
        std::string topic = topicLen > 0 ? std::string(topicName, static_cast<size_t>(topicLen)) : std::string(topicName);
        std::string payload(static_cast<const char*>(message->payload), static_cast<size_t>(message->payloadlen));
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        if (inst.thread && inst.running)
            (void)dmq::MakeDelegate(&MqttBridge::HandleInbound, *inst.thread).AsyncInvoke(std::move(topic), std::move(payload));
        return 1;   // message consumed
    }
};

dmq::Signal<void(const std::string&)>& MqttBridge::OnEvent() {
    return GetInstance().onEvent;
}

std::string MqttBridge::MqttTopic(const std::string& dataBusTopic) {
    const auto& prefix = GetInstance().options.topicPrefix;
    return prefix.empty() ? dataBusTopic : prefix + "/" + dataBusTopic;
}

bool MqttBridge::IsConnected() {
    return GetInstance().connected;
}

void MqttBridge::RaiseEvent(const std::string& text) {
    auto& inst = GetInstance();
    if (inst.onEvent.Empty())
        std::cerr << "[MqttBridge] " << text << std::endl;
    else
        inst.onEvent(text);
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

    // DROP: a slow or unreachable broker must never stall DataBus publishers.
    inst.thread = std::make_unique<dmq::os::Thread>("MqttBridge", BRIDGE_QUEUE_SIZE, dmq::os::FullPolicy::DROP);
    inst.thread->CreateThread();

    inst.running = true;
    inst.supervisor = std::thread(&MqttBridge::SupervisorLoop);
    return true;
}

void MqttBridge::Stop() {
    auto& inst = GetInstance();
    if (!inst.thread) return;

    // No new DataBus work, then no reconnects, then drain the bridge thread.
    {
        std::lock_guard<std::mutex> lock(inst.mutex);
        inst.connections.clear();
    }
    inst.running = false;
    if (inst.supervisor.joinable()) inst.supervisor.join();
    inst.thread->ExitThread();

    // Only this thread uses the client now (Paho's own thread aside).
    MQTTClient client = static_cast<MQTTClient>(inst.client);
    if (inst.connected.exchange(false)) {
        std::string topic = inst.options.topicPrefix + ONLINE_SUFFIX;
        MQTTClient_deliveryToken token = 0;
        if (MQTTClient_publish(client, topic.c_str(), 5, "false", 1, 1, &token) == MQTTCLIENT_SUCCESS)
            MQTTClient_waitForCompletion(client, token, 1000);
        MQTTClient_disconnect(client, 1000);
    }
    MQTTClient_destroy(&client);
    inst.client = nullptr;
    inst.thread.reset();
    {
        std::lock_guard<std::mutex> lock(inst.mutex);
        inst.inbound.clear();
    }
    inst.retained.clear();
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
    will.message = "false";
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

    // Re-subscribe inbound command topics (clean session).
    std::vector<std::pair<std::string, int>> subs;
    {
        std::lock_guard<std::mutex> lock(inst.mutex);
        for (const auto& [topic, in] : inst.inbound)
            subs.emplace_back(topic, in.qos);
    }
    for (const auto& [topic, qos] : subs) {
        if (MQTTClient_subscribe(client, topic.c_str(), qos) != MQTTCLIENT_SUCCESS)
            RaiseEvent("cannot subscribe to " + topic);
    }

    MQTTClient_deliveryToken token = 0;
    MQTTClient_publish(client, onlineTopic.c_str(), 4, "true", 1, 1, &token);

    // Current state that was published while disconnected (or before a broker
    // restart lost it).
    for (const auto& [topic, value] : inst.retained)
        MQTTClient_publish(client, topic.c_str(), static_cast<int>(value.first.size()), value.first.data(),
                           value.second, 1, &token);
    inst.connected = true;
    RaiseEvent("connected to " + inst.options.brokerUri + " as " + inst.options.clientId
               + (subs.empty() ? "" : " (" + std::to_string(subs.size()) + " command topic(s))"));
}

void MqttBridge::SendPayload(const std::string& mqttTopic, const std::string& payload, bool retained, int qos) {
    auto& inst = GetInstance();
    if (retained)
        inst.retained[mqttTopic] = { payload, qos };    // re-sent by Connect()
    if (!inst.connected) {
        ++inst.dropped;
        return;
    }
    MQTTClient_deliveryToken token = 0;
    int rc = MQTTClient_publish(static_cast<MQTTClient>(inst.client), mqttTopic.c_str(),
                                static_cast<int>(payload.size()), payload.data(), qos, retained ? 1 : 0, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        ++inst.dropped;
        if (rc == MQTTCLIENT_DISCONNECTED && inst.connected.exchange(false))
            RaiseEvent("connection lost while publishing " + mqttTopic);
    }
}

void MqttBridge::AddInbound(const std::string& mqttTopic, InboundHandler handler, int qos) {
    auto& inst = GetInstance();
    {
        std::lock_guard<std::mutex> lock(inst.mutex);
        inst.inbound[mqttTopic] = Inbound{ std::move(handler), qos };
    }
    // Already connected: subscribe now, on the bridge thread. Otherwise Connect() does it.
    std::string topic = mqttTopic;
    (void)dmq::MakeDelegate(std::function<void()>([topic, qos]() {
        auto& i = GetInstance();
        if (i.connected && MQTTClient_subscribe(static_cast<MQTTClient>(i.client), topic.c_str(), qos) != MQTTCLIENT_SUCCESS)
            RaiseEvent("cannot subscribe to " + topic);
    }), *inst.thread).AsyncInvoke();
}

void MqttBridge::HandleInbound(std::string mqttTopic, std::string payload) {
    auto& inst = GetInstance();
    InboundHandler handler;
    {
        std::lock_guard<std::mutex> lock(inst.mutex);
        auto it = inst.inbound.find(mqttTopic);
        if (it == inst.inbound.end()) return;
        handler = it->second.handler;
    }
    handler(payload);
}
