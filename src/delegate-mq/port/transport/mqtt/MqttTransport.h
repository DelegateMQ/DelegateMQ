
#ifndef MQTT_TRANSPORT_H
#define MQTT_TRANSPORT_H

/// @file 
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// Transport callable argument data to/from a remote using MQTT library. 
/// https://github.com/eclipse-paho/paho.mqtt.c
///
/// Carries DelegateMQ binary frames between DelegateMQ apps on one MQTT topic.
/// Other MQTT tools can't read these frames; to expose DataBus topics as
/// readable JSON instead, see tools/bridge/mqtt/MqttBridge.
///
/// The broker defaults to a local one (tcp://localhost:1883, e.g. Mosquitto).
/// Pass a broker URI to Create() to use another. Never point real traffic at
/// a public test broker: anyone can read and publish on it.
///
/// @code
/// MqttTransport t;
/// t.Create(MqttTransport::Type::PUB);                                     // local broker, default topic
/// t.Create(MqttTransport::Type::SUB, "tcp://10.0.0.5:1883", "plant/dmq"); // explicit broker and topic
/// @endcode

// Ensure network byte order functions are available
#if defined(_WIN32) || defined(_WIN64)
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
#endif

#include "port/transport/common/ITransport.h"
#include "port/transport/common/ITransportMonitor.h"
#include "port/transport/common/DmqHeader.h"
#include "MQTTClient.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <string>
#include <random>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace dmq::transport {

/// @brief MQTT transport example.
class MqttTransport : public ITransport
{
    XALLOCATOR
public:
    enum class Type
    {
        PUB,
        SUB
    };

    static constexpr const char* DEFAULT_ADDRESS = "tcp://localhost:1883";
    static constexpr const char* DEFAULT_TOPIC   = "dmq/delegate";
    static constexpr int         QOS             = 1;
    static constexpr long        TIMEOUT_MS      = 10000L;

    MqttTransport() = default;

    ~MqttTransport()
    {
        Close();
    }

    /// Connect to the broker.
    /// @param[in] type PUB publishes to topic; SUB subscribes to it.
    /// @param[in] address Broker URI, e.g. "tcp://localhost:1883".
    /// @param[in] topic MQTT topic carrying the frames. Both ends must use the same one.
    /// @param[in] clientId MQTT client ID. Must be unique on the broker: a second
    ///   client with the same ID disconnects the first. nullptr (default)
    ///   generates a random one ("dmq-pub-xxxxxxxx" / "dmq-sub-xxxxxxxx").
    /// @return 0 (MQTTCLIENT_SUCCESS) on success, otherwise a Paho error code.
    int Create(Type type, const char* address = DEFAULT_ADDRESS,
               const char* topic = DEFAULT_TOPIC, const char* clientId = nullptr)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        int rc = EXIT_FAILURE;
        if (address == nullptr || topic == nullptr || *topic == '\0')
            return MQTTCLIENT_NULL_PARAMETER;

        m_topic = topic;
        const std::string id = clientId ? std::string(clientId) : MakeClientId(type);
        printf("MqttTransport: broker %s, topic %s, client %s\n", address, m_topic.c_str(), id.c_str());

        {
            std::lock_guard<std::mutex> q_lock(m_queueMtx);
            m_stop = false;
        }

        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
        m_type = type;
        if (m_type == Type::PUB)
        {
            if ((rc = MQTTClient_create(&m_client, address, id.c_str(),
                MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS)
            {
                m_client = nullptr;
                return rc;
            }

            conn_opts.keepAliveInterval = 20;
            conn_opts.cleansession = 1;
            if ((rc = MQTTClient_connect(m_client, &conn_opts)) != MQTTCLIENT_SUCCESS)
            {
                MQTTClient_destroy(&m_client);
                m_client = nullptr;
                return rc;
            }
        }
        else if (m_type == Type::SUB)
        {
            if ((rc = MQTTClient_create(&m_client, address, id.c_str(),
                MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS)
            {
                m_client = nullptr;
                return rc;
            }

            // Register callbacks to populate internal queue
            if ((rc = MQTTClient_setCallbacks(m_client, this, connlost, msgarrvd, delivered)) != MQTTCLIENT_SUCCESS)
            {
                MQTTClient_destroy(&m_client);
                m_client = nullptr;
                return rc;
            }

            conn_opts.keepAliveInterval = 20;
            conn_opts.cleansession = 1;
            if ((rc = MQTTClient_connect(m_client, &conn_opts)) != MQTTCLIENT_SUCCESS)
            {
                MQTTClient_destroy(&m_client);
                m_client = nullptr;
                return rc;
            }

            if ((rc = MQTTClient_subscribe(m_client, m_topic.c_str(), QOS)) != MQTTCLIENT_SUCCESS)
            {
                MQTTClient_disconnect(m_client, 1000);
                MQTTClient_destroy(&m_client);
                m_client = nullptr;
                return rc;
            }
        }
        return MQTTCLIENT_SUCCESS;
    }

    void Close()
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_client)
        {
            if (m_type == Type::SUB)
            {
                MQTTClient_unsubscribe(m_client, m_topic.c_str());
            }
            MQTTClient_disconnect(m_client, 1000);
            MQTTClient_destroy(&m_client);
            m_client = nullptr;
        }

        // Wake up any blocked Receive
        {
            std::lock_guard<std::mutex> q_lock(m_queueMtx);
            m_stop = true;
        }
        m_queueCv.notify_all();
    }

    virtual int Send(xostringstream& os, const DmqHeader& header) override
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (os.bad() || os.fail() || !m_client)
            return -1;

        // Get payload data without string copy if possible
        auto payload = os.str();
        if (payload.length() > (BUFFER_SIZE - DmqHeader::HEADER_SIZE)) {
            return -1;
        }
        uint16_t payloadLen = static_cast<uint16_t>(payload.length());

        // Prepare header values in Network Byte Order
        uint16_t marker = htons(header.GetMarker());
        uint16_t id     = htons(header.GetId());
        uint16_t seqNum = htons(header.GetSeqNum());
        uint16_t length = htons(payloadLen);

        // Copy Header & Payload into the linear buffer
        memcpy(m_sendBuffer, &marker, 2);
        memcpy(m_sendBuffer + 2, &id, 2);
        memcpy(m_sendBuffer + 4, &seqNum, 2);
        memcpy(m_sendBuffer + 6, &length, 2);

        if (payloadLen > 0) {
            memcpy(m_sendBuffer + DmqHeader::HEADER_SIZE, payload.data(), payloadLen);
        }

        size_t totalSize = DmqHeader::HEADER_SIZE + payloadLen;

        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        MQTTClient_deliveryToken token;
        
        pubmsg.payload = (void*)m_sendBuffer;
        pubmsg.payloadlen = (int)totalSize;
        pubmsg.qos = QOS;
        pubmsg.retained = 0;

        int rc;
        if ((rc = MQTTClient_publishMessage(m_client, m_topic.c_str(), &pubmsg, &token)) != MQTTCLIENT_SUCCESS)
        {
            return EXIT_FAILURE;
        }
        else
        {
            return EXIT_SUCCESS;
        }
    }

    virtual int Receive(xstringstream& is, DmqHeader& header) override
    {
        std::unique_lock<std::mutex> q_lock(m_queueMtx);
        
        // Wait for data or stop signal
        if (m_queueCv.wait_for(q_lock, m_recvTimeout, [this] { return !m_rxQueue.empty() || m_stop; }))
        {
            if (m_stop) return -1;

            if (m_rxQueue.empty()) return -1;

            // Pop message
            std::vector<char> msg = std::move(m_rxQueue.front());
            m_rxQueue.pop();
            q_lock.unlock(); // Release queue lock before processing

            if (msg.size() < DmqHeader::HEADER_SIZE) return -1;

            // 1. Read Header (Network Byte Order)
            uint16_t marker, id, seqNum, length;
            memcpy(&marker, msg.data(), 2);
            memcpy(&id,     msg.data() + 2, 2);
            memcpy(&seqNum, msg.data() + 4, 2);
            memcpy(&length, msg.data() + 6, 2);

            header.SetMarker(ntohs(marker));
            header.SetId(ntohs(id));
            header.SetSeqNum(ntohs(seqNum));
            header.SetLength(ntohs(length));

            if (header.GetMarker() != DmqHeader::MARKER) return -1;

            // 2. Write payload
            uint16_t payloadSize = header.GetLength();
            if (payloadSize > (msg.size() - DmqHeader::HEADER_SIZE))
                payloadSize = static_cast<uint16_t>(msg.size() - DmqHeader::HEADER_SIZE);

            if (payloadSize > 0) {
                is.clear();
                is.str("");
                is.write(msg.data() + DmqHeader::HEADER_SIZE, payloadSize);
            }

            // 3. ACK Logic
            if (header.GetId() == dmq::ACK_REMOTE_ID) {
                if (m_transportMonitor) m_transportMonitor->Remove(header.GetSeqNum());
            }
            else if (m_transportMonitor) {
                // Auto-ACK
                xostringstream ss_ack;
                DmqHeader ack;
                ack.SetId(dmq::ACK_REMOTE_ID);
                ack.SetSeqNum(header.GetSeqNum());
                Send(ss_ack, ack);
            }
            return 0;
        }

        return -1; // Timeout
    }

    void SetTransportMonitor(ITransportMonitor* transportMonitor)
    {
        m_transportMonitor = transportMonitor;
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        m_recvTimeout = timeout;
    }

private:
    /// Random client ID, so two apps (or two runs) never share one by accident.
    static std::string MakeClientId(Type type)
    {
        static const char hex[] = "0123456789abcdef";
        std::random_device rd;
        std::string id = (type == Type::PUB) ? "dmq-pub-" : "dmq-sub-";
        for (int i = 0; i < 8; ++i)
            id += hex[rd() & 0xF];
        return id;
    }

    static void delivered(void* context, MQTTClient_deliveryToken dt)
    {
        // printf("Message with token value %d delivery confirmed\n", dt);
    }

    // Callback running on Paho Thread
    static int msgarrvd(void* context, char* topicName, int topicLen, MQTTClient_message* message)
    {
        MqttTransport* instance = (MqttTransport*)context;
        if (instance)
        {
            // Copy data to vector and push to queue
            std::vector<char> data;
            data.resize(message->payloadlen);
            std::memcpy(data.data(), message->payload, message->payloadlen);

            {
                std::lock_guard<std::mutex> lock(instance->m_queueMtx);
                instance->m_rxQueue.push(std::move(data));
            }
            instance->m_queueCv.notify_one();
        }

        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        return 1;
    }

    static void connlost(void* context, char* cause)
    {
        // printf("\nConnection lost\n");
    }

    MQTTClient m_client = nullptr;
    std::string m_topic = DEFAULT_TOPIC;
    ITransportMonitor* m_transportMonitor = nullptr;
    Type m_type = Type::PUB;

    std::chrono::milliseconds m_recvTimeout = std::chrono::milliseconds(1000);

    // Thread-safe Queue for bridging Paho callback to ITransport::Receive
    std::queue<std::vector<char>> m_rxQueue;
    std::mutex m_queueMtx;
    std::condition_variable m_queueCv;
    bool m_stop = false;

    static const int BUFFER_SIZE = 4096;
    char m_sendBuffer[BUFFER_SIZE] = { 0 };
    dmq::RecursiveMutex m_mutex;
};

}

#endif // MQTT_TRANSPORT_H