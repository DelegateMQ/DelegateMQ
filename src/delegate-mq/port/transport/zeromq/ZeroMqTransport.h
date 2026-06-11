#ifndef ZEROMQ_TRANSPORT_H
#define ZEROMQ_TRANSPORT_H

/// @file ZeroMqTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief ZeroMQ transport implementation for DelegateMQ.
/// 
/// @details
/// This class implements the ITransport interface using the ZeroMQ high-performance 
/// asynchronous messaging library. It supports multiple patterns including PAIR (1-to-1) 
/// and PUB/SUB (1-to-many).
/// 
/// Key Features:
/// 1. **Thread Safety**: Uses a `dmq::RecursiveMutex` to protect the ZeroMQ socket, 
///    allowing safe concurrent access from the Send and Receive threads (ZeroMQ sockets 
///    are not inherently thread-safe).
/// 2. **Flexible Patterns**: Supports both bidirectional (PAIR) and unidirectional (PUB/SUB)
///    communication topologies.
/// 3. **Non-Blocking**: Configures `ZMQ_RCVTIMEO` and `ZMQ_DONTWAIT` to ensure the 
///    transport remains responsive and never blocks the application indefinitely.
/// 4. **Reliability**: Integrates with `TransportMonitor` to provide sequence tracking 
///    and ACKs even over PUB/SUB (when a return channel is available).
/// 
/// @note Requires the `libzmq` library.

// Add Windows Sockets headers for htons/ntohs
#if defined(_WIN32) || defined(_WIN64)
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
#endif

#include "delegate/DelegateOpt.h"
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include "port/transport/ITransportMonitor.h"
#include <zmq.h>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <iostream> // For std::cout/cerr

namespace dmq::transport {

/// @brief ZeroMQ transport class.
/// @details Logic now executes directly on the caller's thread, protected by a mutex
/// to prevent concurrent access to the underlying ZeroMQ socket.
class ZeroMqTransport : public ITransport
{
public:
    enum class Type
    {
        PAIR_CLIENT,
        PAIR_SERVER,
        PUB,
        SUB
    };

    ZeroMqTransport() : m_sendTransport(this), m_recvTransport(this)
    {
    }

    ~ZeroMqTransport()
    {
        Destroy();
    }

    int Create(Type type, const char* addr)
    {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

        if (m_zmqContext == nullptr) {
            m_zmqContext = zmq_ctx_new();
        }

        m_type = type;

        if (m_type == Type::PAIR_CLIENT)
        {
            // Create a PAIR socket and connect the client to the server address
            m_zmq = zmq_socket(m_zmqContext, ZMQ_PAIR);
            int rc = zmq_connect(m_zmq, addr);
            if (rc != 0) {
                // perror is less useful on Windows for ZMQ specific errors, but ok for console
                perror("zmq_connect failed");
                zmq_close(m_zmq);
                return 1;
            }

            // Set the receive timeout to 100 milliseconds
            int timeout = 100; // 100mS 
            zmq_setsockopt(m_zmq, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
        }
        else if (m_type == Type::PAIR_SERVER)
        {
            // Create a PAIR socket and bind the server to an address
            m_zmq = zmq_socket(m_zmqContext, ZMQ_PAIR);
            int rc = zmq_bind(m_zmq, addr);
            if (rc != 0) {
                perror("zmq_bind failed");
                return 1;
            }

            // Set the receive timeout to 100 milliseconds
            int timeout = 100; // 100mS 
            zmq_setsockopt(m_zmq, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
        }
        else if (m_type == Type::PUB)
        {
            // Create a PUB socket
            m_zmq = zmq_socket(m_zmqContext, ZMQ_PUB);
            int rc = zmq_bind(m_zmq, addr);
            if (rc != 0) {
                perror("zmq_bind failed");
                return 1;
            }
        }
        else if (m_type == Type::SUB)
        {
            // Create a SUB socket
            m_zmq = zmq_socket(m_zmqContext, ZMQ_SUB);
            int rc = zmq_connect(m_zmq, addr);
            if (rc != 0) {
                perror("zmq_connect failed");
                zmq_close(m_zmq);
                return 1;
            }

            // Subscribe to all messages
            zmq_setsockopt(m_zmq, ZMQ_SUBSCRIBE, "", 0);

            // Set the receive timeout to 100 milliseconds
            int timeout = 100; // 100mS 
            zmq_setsockopt(m_zmq, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
        }
        else
        {
            // Handle other types, if needed
            perror("Invalid socket type");
            return 1;
        }
        return 0;
    }

    void Close()
    {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

        // Close the subscriber socket and context
        if (m_zmq) {
            // Linger 0 ensures close doesn't block waiting for unsent messages
            int linger = 0;
            zmq_setsockopt(m_zmq, ZMQ_LINGER, &linger, sizeof(linger));
            zmq_close(m_zmq);
            m_zmq = nullptr;
        }
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_zmq)
        {
            int ms = static_cast<int>(timeout.count());
            zmq_setsockopt(m_zmq, ZMQ_RCVTIMEO, &ms, sizeof(ms));
        }
    }

    void Destroy()
    {
        Close();

        // Context termination is thread-safe in ZMQ
        if (m_zmqContext) {
            zmq_ctx_term(m_zmqContext);
            m_zmqContext = nullptr;
        }
    }

    virtual int Send(dmq::xostringstream& os, const DmqHeader& header) override
    {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

        if (os.bad() || os.fail() || m_zmq == nullptr) {
            return -1;
        }

        if (m_type == Type::SUB) {
            return -1;
        }

        if (m_sendTransport != this) {
            return -1;
        }

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

        // Send data using ZeroMQ
        int err = zmq_send(m_zmq, m_sendBuffer, totalSize, ZMQ_DONTWAIT);
        if (err == -1)
        {
            // EAGAIN is common if queue is full
            return -1;
        }

        return 0;
    }

    virtual int Receive(dmq::xstringstream& is, DmqHeader& header) override
    {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

        if (m_zmq == nullptr) {
            return -1;
        }

        if (m_type == Type::PUB) {
            return -1;
        }

        if (m_recvTransport != this) {
            return -1;
        }

        int size = zmq_recv(m_zmq, m_buffer, BUFFER_SIZE, 0);
        if (size == -1) {
            return -1;
        }

        if (size < DmqHeader::HEADER_SIZE) {
            return -1;
        }

        // 1. Read Header (Network Byte Order)
        uint16_t marker, id, seqNum, length;
        memcpy(&marker, m_buffer, 2);
        memcpy(&id,     m_buffer + 2, 2);
        memcpy(&seqNum, m_buffer + 4, 2);
        memcpy(&length, m_buffer + 6, 2);

        header.SetMarker(ntohs(marker));
        header.SetId(ntohs(id));
        header.SetSeqNum(ntohs(seqNum));
        header.SetLength(ntohs(length));

        if (header.GetMarker() != DmqHeader::MARKER) {
            return -1;
        }

        // 2. Write payload
        uint16_t payloadSize = header.GetLength();
        if (payloadSize > (size - DmqHeader::HEADER_SIZE))
            payloadSize = static_cast<uint16_t>(size - DmqHeader::HEADER_SIZE);

        if (payloadSize > 0) {
            is.clear();
            is.str("");
            is.write(m_buffer + DmqHeader::HEADER_SIZE, payloadSize);
        }

        if (header.GetId() == dmq::ACK_REMOTE_ID)
        {
            // Receiver ack'ed message. Remove sequence number from monitor.
            if (m_transportMonitor)
                m_transportMonitor->Remove(header.GetSeqNum());
        }
        else
        {
            if (m_transportMonitor && m_sendTransport)
            {
                // Send header with received seqNum as the ack message
                dmq::xostringstream ss_ack;
                DmqHeader ack;
                ack.SetId(dmq::ACK_REMOTE_ID);
                ack.SetSeqNum(header.GetSeqNum());
                
                // Note: Recursive mutex allows Send() here
                m_sendTransport->Send(ss_ack, ack);
            }
        }

        return 0;  // Success
    }

    // Set a transport monitor for checking message ack
    void SetTransportMonitor(ITransportMonitor* transportMonitor)
    {
        m_transportMonitor = transportMonitor;
    }

    // Set an alternative send transport
    void SetSendTransport(ITransport* sendTransport)
    {
        m_sendTransport = sendTransport;
    }

    // Set an alternative receive transport
    void SetRecvTransport(ITransport* recvTransport)
    {
        m_recvTransport = recvTransport;
    }

private:
    void* m_zmqContext = nullptr;
    void* m_zmq = nullptr;
    Type m_type = Type::PAIR_CLIENT;

    dmq::RecursiveMutex m_mutex;

    ITransport* m_sendTransport = nullptr;
    ITransport* m_recvTransport = nullptr;
    ITransportMonitor* m_transportMonitor = nullptr;

    static const int BUFFER_SIZE = 4096;
    char m_buffer[BUFFER_SIZE] = {};
    char m_sendBuffer[BUFFER_SIZE] = {};
};

} // namespace dmq::transport

#endif
