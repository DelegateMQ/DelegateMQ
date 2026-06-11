#ifndef ZEPHYR_UDP_TRANSPORT_H
#define ZEPHYR_UDP_TRANSPORT_H

/// @file ZephyrUdpTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
/// 
/// @brief Zephyr RTOS UDP transport implementation for DelegateMQ.
/// 
/// @details
/// This class implements the ITransport interface using the Zephyr Networking Subsystem
/// (BSD Socket API). It is designed for embedded targets running Zephyr RTOS.
/// 
/// **Prerequisites:**
/// * Enable BSD Sockets: `CONFIG_NET_SOCKETS=y`
/// * Enable POSIX Names: `CONFIG_NET_SOCKETS_POSIX_NAMES=y` (Default)
/// * Enable IPv4: `CONFIG_NET_IPV4=y`
/// 
/// **Key Features:**
/// 1. **Direct Execution**: Executes network operations directly on the calling thread,
///    avoiding context switch overhead and preventing deadlocks.
/// 2. **Zero-Copy Friendly**: Uses the standard BSD API which Zephyr optimizes internally.
/// 3. **Reliability**: Fully integrated with `TransportMonitor` for ACKs/Retries.
/// 4. **Endianness**: Uses `htons`/`ntohs` for standard network byte order compatibility.

#include "delegate/DelegateOpt.h"
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include "port/transport/ITransportMonitor.h"

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <mutex>

namespace dmq::transport {

class ZephyrUdpTransport : public ITransport
{
public:
    enum class Type
    {
        PUB,
        SUB
    };

    ZephyrUdpTransport() : m_sendTransport(this), m_recvTransport(this)
    {
    }

    ~ZephyrUdpTransport()
    {
        Close();
    }

    int Create(Type type, const char* addr, uint16_t port)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_type = type;

        // Create UDP socket using Zephyr BSD API
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket < 0)
        {
            // printk("Socket creation failed: %d\n", errno);
            return -1;
        }

        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin_family = AF_INET;
        m_addr.sin_port = htons(port);

        if (type == Type::PUB)
        {
            // inet_pton is standard in Zephyr's socket.h
            if (inet_pton(AF_INET, addr, &m_addr.sin_addr) != 1)
            {
                // printk("Invalid IP address format.\n");
                Close();
                return -1;
            }

            // Set a short timeout (e.g. 50ms) so Sender::Send() doesn't hang
            // while polling for ACKs.
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000; // 50ms

            if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
            {
                // printk("setsockopt(SO_RCVTIMEO) failed\n");
                Close();
                return -1;
            }
        }
        else if (type == Type::SUB)
        {
            m_addr.sin_addr.s_addr = INADDR_ANY;

            if (bind(m_socket, (struct sockaddr*)&m_addr, sizeof(m_addr)) < 0)
            {
                // printk("Bind failed: %d\n", errno);
                Close();
                return -1;
            }

            // Set a 2-second receive timeout
            struct timeval timeout;
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;

            if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
            {
                // printk("setsockopt(SO_RCVTIMEO) failed\n");
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
            // zsock_shutdown helps wake up blocked threads
            zsock_shutdown(m_socket, ZSOCK_SHUT_RDWR);
            zsock_close(m_socket);
            m_socket = -1;
        }
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_socket >= 0)
        {
            struct timeval tv;
            tv.tv_sec = timeout.count() / 1000;
            tv.tv_usec = (timeout.count() % 1000) * 1000;
            setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
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

        // Convert to Network Byte Order (Big Endian)
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

        ssize_t sent = sendto(m_socket, m_sendBuffer, totalSize, 0,
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
        
        ssize_t size = recvfrom(m_socket, m_buffer, sizeof(m_buffer), 0,
            (struct sockaddr*)&fromAddr, &addrLen);

        if (size < 0)
        {
            // Zephyr uses standard errno values
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                return -1; // Timeout
            
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
            return -1; // Invalid marker
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

    static const int BUFFER_SIZE = 1500; // Ethernet MTU size
    char m_buffer[BUFFER_SIZE] = { 0 };
    char m_sendBuffer[BUFFER_SIZE] = { 0 };
    dmq::RecursiveMutex m_mutex;
};

}

#endif // ZEPHYR_UDP_TRANSPORT_H