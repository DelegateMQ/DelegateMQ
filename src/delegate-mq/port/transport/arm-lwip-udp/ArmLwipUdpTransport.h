#ifndef ARM_LWIP_UDP_TRANSPORT_H
#define ARM_LWIP_UDP_TRANSPORT_H

/// @file ArmLwipUdpTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
/// 
/// @brief ARM lwIP UDP transport implementation for DelegateMQ.
/// 
/// @details
/// This class implements the ITransport interface using the lwIP (Lightweight IP) 
/// BSD-style socket API. It is designed for embedded ARM targets running FreeRTOS 
/// (or similar) with the lwIP stack.
/// 
/// Prerequisites:
/// - lwIP must be compiled with `LWIP_SOCKET=1`
/// - lwIP must be compiled with `LWIP_SO_RCVTIMEO=1` (for non-blocking timeouts)

#include "delegate/DelegateOpt.h"
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include "port/transport/ITransportMonitor.h"

// lwIP Includes
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/sys.h"
#include "lwip/api.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cerrno> 
#include <mutex>

// Ensure lwIP is configured correctly
#if !defined(LWIP_SO_RCVTIMEO) || LWIP_SO_RCVTIMEO == 0
    #error "ArmLwipUdpTransport requires LWIP_SO_RCVTIMEO=1 in lwipopts.h"
#endif

namespace dmq::transport {

class UdpTransport : public ITransport
{
public:
    enum class Type
    {
        PUB,
        SUB
    };

    UdpTransport() : m_sendTransport(this), m_recvTransport(this)
    {
    }

    ~UdpTransport()
    {
        Close();
    }

    int Create(Type type, const char* addr, uint16_t port)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_type = type;

        // Create lwIP UDP socket
        m_socket = lwip_socket(AF_INET, SOCK_DGRAM, 0);
        if (m_socket < 0)
        {
            return -1;
        }

        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin_family = AF_INET;
        m_addr.sin_port = htons(port);

        if (type == Type::PUB)
        {
            // Note: inet_aton in lwIP returns 1 on success
            if (inet_aton(addr, &m_addr.sin_addr) == 0)
            {
                Close(); 
                return -1;
            }

            // Set a short timeout (e.g. 50ms) so Sender::Send() doesn't hang
            // while polling for ACKs.
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000; // 50ms

            if (lwip_setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
            {
                Close(); 
                return -1;
            }
        }
        else if (type == Type::SUB)
        {
            m_addr.sin_addr.s_addr = INADDR_ANY;

            if (lwip_bind(m_socket, (struct sockaddr*)&m_addr, sizeof(m_addr)) < 0)
            {
                Close(); 
                return -1;
            }

            // Set a 2 second receive timeout to allow thread exit checks
            struct timeval timeout;
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;

            if (lwip_setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
            {
                Close(); 
                return -1;
            }
        }

        return 0;
    }

    void Close()
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_socket >= 0)
        {
            lwip_close(m_socket);
            m_socket = -1;
        }
    }

    virtual int Send(xostringstream& os, const DmqHeader& header) override
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (os.bad() || os.fail() || m_socket < 0) {
            return -1;
        }

        // Allow ACKs on SUB sockets. Block only regular data.
        if (m_type == Type::SUB && header.GetId() != dmq::ACK_REMOTE_ID) {
            return -1;
        }

        if (m_sendTransport != this) {
            return -1;
        }

        // Get payload and set length on the copy
        auto payload = os.str();
        uint16_t payloadLen = static_cast<uint16_t>(payload.length());

        if (payloadLen > (BUFFER_SIZE - DmqHeader::HEADER_SIZE)) {
            return -1;
        }

        // Convert header fields to Network Byte Order (Big Endian) 
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

        ssize_t sent = lwip_sendto(m_socket, m_sendBuffer, totalSize, 0,
            (struct sockaddr*)&m_addr, sizeof(m_addr));
        
        if (sent != (ssize_t)totalSize) return -1;

        return 0;
    }

    virtual int Receive(xstringstream& is, DmqHeader& header) override
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_recvTransport != this || m_socket < 0) {
            return -1;
        }

        sockaddr_in fromAddr;
        socklen_t addrLen = sizeof(fromAddr);
        
        // lwip_recvfrom
        ssize_t size = lwip_recvfrom(m_socket, m_buffer, sizeof(m_buffer), 0,
            (struct sockaddr*)&fromAddr, &addrLen);

        if (size < 0)
        {
            // Explicit check for timeout vs real error
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                return -1; // Timeout (No data)
            
            // Real socket error
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

        if (header.GetMarker() != DmqHeader::MARKER)
        {
            return -1; // Sync marker mismatch
        }

        // Important: Update m_addr to the sender's address so we can ACK back
        if (m_type == Type::SUB) {
            m_addr = fromAddr;
        }

        // 2. Extract Payload
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
            if (m_transportMonitor)
                m_transportMonitor->Remove(header.GetSeqNum());
        }
        else if (m_transportMonitor && m_sendTransport)
        {
            // Send ACK back using Send logic (recursive mutex handles it)
            xostringstream ss_ack;
            DmqHeader ack;
            ack.SetId(dmq::ACK_REMOTE_ID);
            ack.SetSeqNum(header.GetSeqNum());
            ack.SetLength(0);
            m_sendTransport->Send(ss_ack, ack);
        }

        return 0;
    }

    void SetTransportMonitor(ITransportMonitor* transportMonitor)
    {
        m_transportMonitor = transportMonitor;
    }

    void SetSendTransport(ITransport* sendTransport)
    {
        m_sendTransport = sendTransport;
    }

    void SetRecvTransport(ITransport* recvTransport)
    {
        m_recvTransport = recvTransport;
    }

private:
    int m_socket = -1;
    sockaddr_in m_addr{};
    Type m_type = Type::PUB;

    ITransport* m_sendTransport = nullptr;
    ITransport* m_recvTransport = nullptr;
    ITransportMonitor* m_transportMonitor = nullptr;

    // Reduced to 1500 (Standard Ethernet MTU) for embedded environments
    static const int BUFFER_SIZE = 1500;
    char m_buffer[BUFFER_SIZE] = { 0 };
    char m_sendBuffer[BUFFER_SIZE] = { 0 };
    dmq::RecursiveMutex m_mutex;
};

}

#endif // ARM_LWIP_UDP_TRANSPORT_H