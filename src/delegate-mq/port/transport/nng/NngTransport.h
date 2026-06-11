#ifndef NNG_TRANSPORT_H
#define NNG_TRANSPORT_H

/// @file NngTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief NNG transport implementation for DelegateMQ.
/// 
/// @details
/// This class implements the ITransport interface using the NNG (Nanomsg Next Gen) 
/// lightweight messaging library. It supports multiple scalability protocols including 
/// PAIR (1-to-1 bidrectional) and PUB/SUB (1-to-many unidirectional).
/// 
/// Key Features:
/// 1. **Thread Safety**: Uses a `dmq::RecursiveMutex` to protect the NNG socket, 
///    allowing safe concurrent access from the Send and Receive threads (NNG sockets 
///    are not inherently thread-safe).
/// 2. **Scalability Protocols**: flexible configuration for different topologies:
///    * `PAIR_CLIENT`/`PAIR_SERVER`: Exclusive 1-to-1 connection.
///    * `PUB`/`SUB`: Efficient distribution to multiple subscribers.
/// 3. **Non-Blocking**: Uses asynchronous messaging patterns provided by NNG.
/// 4. **Reliability**: Integrates with `TransportMonitor` to providing sequence tracking 
///    and ACKs even over PUB/SUB (when a return channel is available).
/// 
/// @note Requires the `libnng` library.

// Add Windows Sockets headers for htons/ntohs
#if defined(_WIN32) || defined(_WIN64)
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
#endif

#include "delegate/DelegateOpt.h"
#include "port/transport/ITransport.h"
#include "port/transport/ITransportMonitor.h"
#include "port/transport/DmqHeader.h"
#include <nng/nng.h>
#include <nng/protocol/pair0/pair.h>
#include <nng/protocol/pubsub0/pub.h>
#include <nng/protocol/pubsub0/sub.h>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <iostream>

namespace dmq::transport {

/// @brief NNG transport class. 
/// @details Logic now executes directly on the caller's thread, protected by a mutex
/// to prevent concurrent access to the underlying NNG socket.
class NngTransport : public ITransport
{
public:
    enum class Type
    {
        PAIR_CLIENT,
        PAIR_SERVER,
        PUB,
        SUB
    };

    NngTransport() : m_sendTransport(this), m_recvTransport(this)
    {
    }

    ~NngTransport()
    {
        Destroy();
    }

    int Create(Type type, const char* addr)
    {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

        // Initialize NNG context
        m_type = type;

        int rc = 0;
        if (m_type == Type::PAIR_CLIENT)
        {
            rc = nng_pair0_open(&m_nngSocket);
            if (rc != 0) {
                std::cerr << "Failed to open NNG pair client socket: " << nng_strerror(rc) << std::endl;
                return rc;
            }
            rc = nng_dial(m_nngSocket, addr, nullptr, 0);
            if (rc != 0) {
                std::cerr << "Failed to dial address: " << nng_strerror(rc) << std::endl;
                nng_close(m_nngSocket);
                return rc;
            }
            // Set recv timeout to avoid blocking forever
            nng_duration timeout = 100; // 100ms
            nng_socket_set_ms(m_nngSocket, NNG_OPT_RECVTIMEO, timeout);
        }
        else if (m_type == Type::PAIR_SERVER)
        {
            rc = nng_pair0_open(&m_nngSocket);
            if (rc != 0) {
                std::cerr << "Failed to open NNG pair server socket: " << nng_strerror(rc) << std::endl;
                return rc;
            }
            rc = nng_listen(m_nngSocket, addr, nullptr, 0);
            if (rc != 0) {
                std::cerr << "Failed to listen on address: " << nng_strerror(rc) << std::endl;
                return rc;
            }
            // Set recv timeout
            nng_duration timeout = 100; // 100ms
            nng_socket_set_ms(m_nngSocket, NNG_OPT_RECVTIMEO, timeout);
        }
        else if (m_type == Type::PUB)
        {
            rc = nng_pub0_open(&m_nngSocket);
            if (rc != 0) {
                std::cerr << "Failed to open NNG PUB socket: " << nng_strerror(rc) << std::endl;
                return rc;
            }
            rc = nng_listen(m_nngSocket, addr, nullptr, 0);
            if (rc != 0) {
                std::cerr << "Failed to listen on address: " << nng_strerror(rc) << std::endl;
                return rc;
            }
        }
        else if (m_type == Type::SUB)
        {
            rc = nng_sub0_open(&m_nngSocket);
            if (rc != 0) {
                std::cerr << "Failed to open NNG SUB socket: " << nng_strerror(rc) << std::endl;
                return rc;
            }
            rc = nng_dial(m_nngSocket, addr, nullptr, 0);
            if (rc != 0) {
                std::cerr << "Failed to dial address: " << nng_strerror(rc) << std::endl;
                nng_close(m_nngSocket);
                return rc;
            }

            // Subscribe to all messages
            rc = nng_socket_set(m_nngSocket, NNG_OPT_SUB_SUBSCRIBE, "", 0);
            if (rc != 0) {
                std::cerr << "Failed to set subscription filter: " << nng_strerror(rc) << std::endl;
                return rc;
            }
            
            // Set recv timeout
            nng_duration timeout = 100; // 100ms
            nng_socket_set_ms(m_nngSocket, NNG_OPT_RECVTIMEO, timeout);
        }
        else
        {
            std::cerr << "Invalid socket type" << std::endl;
            return 1;
        }

        return 0;
    }

    void Close()
    {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

        // Close the socket
        if (nng_socket_id(m_nngSocket) != 0) {
            nng_close(m_nngSocket);
        }
        m_nngSocket = NNG_SOCKET_INITIALIZER;
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (nng_socket_id(m_nngSocket) != 0)
        {
            nng_duration ms = static_cast<nng_duration>(timeout.count());
            nng_socket_set_ms(m_nngSocket, NNG_OPT_RECVTIMEO, ms);
        }
    }

    void Destroy()
    {
        Close();
        // NNG doesn't require explicit context terminations like ZMQ
    }

    virtual int Send(dmq::xostringstream& os, const DmqHeader& header) override
    {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

        if (os.bad() || os.fail() || nng_socket_id(m_nngSocket) == 0) {
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

        // Send data using NNG
        // Use NNG_FLAG_NONBLOCK to prevent deadlocks if peer is gone
        int err = nng_send(m_nngSocket, (void*)m_sendBuffer, totalSize, NNG_FLAG_NONBLOCK);
        if (err != 0)
        {
            // NNG_EAGAIN means queue full (congestion), treat as error to trigger retry logic
            return -1;
        }

        return 0;
    }

    virtual int Receive(xstringstream& is, DmqHeader& header) override {
        // Lock Guard
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

        if (nng_socket_id(m_nngSocket) == 0) {
            return -1;
        }

        if (m_type == Type::PUB) {
            return -1;
        }

        if (m_recvTransport != this) {
            return -1;
        }

        size_t size = sizeof(m_buffer);
        
        // Receive with 100ms timeout (set in Create)
        int err = nng_recv(m_nngSocket, m_buffer, &size, 0);
        if (err != 0) {
            return -1; // EAGAIN or fatal
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
    nng_socket m_nngSocket = NNG_SOCKET_INITIALIZER;
    Type m_type = Type::PAIR_CLIENT;

    // Mutex to protect non-thread-safe socket
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
