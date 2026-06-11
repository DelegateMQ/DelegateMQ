
#ifndef MQTT_TRANSPORT_H
#define MQTT_TRANSPORT_H

/// @file 
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// Transport callable argument data to/from a remote using MQTT library. 
/// https://github.com/eclipse-paho/paho.mqtt.c

// Ensure network byte order functions are available
#if defined(_WIN32) || defined(_WIN64)
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
#endif

#include "port/transport/ITransport.h"
#include "port/transport/ITransportMonitor.h"
#include "port/transport/DmqHeader.h"
#include "MQTTClient.h"

#include <iostream>
#include <sstream>
#include <cstring>
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

    static constexpr const char* ADDRESS       = "tcp://broker.hivemq.com:1883";
    static constexpr const char* TOPIC         = "Delegate_MQTT";
    static constexpr int         QOS           = 1;
    static constexpr long        TIMEOUT_MS    = 10000L;
    static constexpr const char* PUB_CLIENTID  = "DelegatePub";
    static constexpr const char* SUB_CLIENTID  = "DelegateSub";

    MqttTransport() = default;

    ~MqttTransport()
    {
        Close();
    }

    int Create(Type type)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        int rc = EXIT_FAILURE;
        printf("Using server at %s\n", ADDRESS);

        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
        m_type = type;
        if (m_type == Type::PUB)
        {
            if ((rc = MQTTClient_create(&m_client, ADDRESS, PUB_CLIENTID,
                MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS)
            {
                // printf("Failed to create client, return code %d\n", rc);
                return rc;
            }

            conn_opts.keepAliveInterval = 20;
            conn_opts.cleansession = 1;
            if ((rc = MQTTClient_connect(m_client, &conn_opts)) != MQTTCLIENT_SUCCESS)
            {
                // printf("Failed to connect, return code %d\n", rc);
                return rc;
            }
        }
        else if (m_type == Type::SUB)
        {
            if ((rc = MQTTClient_create(&m_client, ADDRESS, SUB_CLIENTID,
                MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS)
            {
                // printf("Failed to create client, return code %d\n", rc);
                return rc;
            }

            // Register callbacks to populate internal queue
            if ((rc = MQTTClient_setCallbacks(m_client, this, connlost, msgarrvd, delivered)) != MQTTCLIENT_SUCCESS)
            {
                // printf("Failed to set callbacks, return code %d\n", rc);
                MQTTClient_destroy(&m_client);
                return rc;
            }

            conn_opts.keepAliveInterval = 20;
            conn_opts.cleansession = 1;
            if ((rc = MQTTClient_connect(m_client, &conn_opts)) != MQTTCLIENT_SUCCESS)
            {
                // printf("Failed to connect, return code %d\n", rc);
                MQTTClient_destroy(&m_client);
                return rc;
            }

            if ((rc = MQTTClient_subscribe(m_client, TOPIC, QOS)) != MQTTCLIENT_SUCCESS)
            {
                // printf("Failed to subscribe, return code %d\n", rc);
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
                MQTTClient_unsubscribe(m_client, TOPIC);
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
        uint16_t payloadLen = static_cast<uint16_t>(payload.length());
        
        if (payloadLen > (BUFFER_SIZE - DmqHeader::HEADER_SIZE)) {
            return -1;
        }

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
        if ((rc = MQTTClient_publishMessage(m_client, TOPIC, &pubmsg, &token)) != MQTTCLIENT_SUCCESS)
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