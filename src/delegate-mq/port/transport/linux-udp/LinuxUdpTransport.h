#ifndef LINUX_UDP_TRANSPORT_H
#define LINUX_UDP_TRANSPORT_H

/// @file LinuxUdpTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief Linux UDP transport implementation for DelegateMQ.
/// 
/// @details
/// This class implements the ITransport interface using standard Linux BSD sockets
/// for connectionless UDP communication. It supports both PUB (Publisher/Sender)
/// and SUB (Subscriber/Receiver) modes.
/// 
/// Key Features:
/// 1. **Direct Execution**: Executes socket operations directly on the calling thread, 
///    relying on OS-level thread safety to avoid deadlocks.
/// 2. **Reliability Support**: Integrates with `TransportMonitor` to track outgoing 
///    sequence numbers and process incoming ACKs to detect packet loss.
/// 3. **Non-Blocking I/O**: Configures socket receive timeouts (`SO_RCVTIMEO`) to 
///    prevent indefinite blocking during polling loops.
/// 4. **Address Management**: Automatically updates the target address on the receiver 
///    side to support reliable bidirectional ACKs.
/// 
/// @note This class is specific to Linux and uses POSIX socket APIs.

#include "delegate/DelegateOpt.h"
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include "port/transport/ITransportMonitor.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <mutex>

namespace dmq::transport {

// Linux UDP transport example
class LinuxUdpTransport : public ITransport
{
public:
    enum class Type
    {
        PUB,
        SUB
    };

    LinuxUdpTransport() : m_sendTransport(this), m_recvTransport(this)
    {
    }

    ~LinuxUdpTransport()
    {
        Close();
    }

    int Create(Type type, const char* addr, uint16_t port)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_type = type;

        // Create UDP socket
        m_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_socket < 0)
        {
            std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
            return -1;
        }

        int opt = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin_family = AF_INET;
        m_addr.sin_port = htons(port);

        if (type == Type::PUB)
        {
            if (inet_aton(addr, &m_addr.sin_addr) == 0)
            {
                std::cerr << "Invalid IP address format." << std::endl;
                return -1;
            }

            // Set a short timeout (e.g. 2ms) so Sender::Send() doesn't hang
            // while polling for ACKs.
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 2000; // 2ms

            if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
            {
                std::cerr << "setsockopt(SO_RCVTIMEO) failed: " << strerror(errno) << std::endl;
                return -1;
            }
        }
        else if (type == Type::SUB)
        {
            if (inet_aton(addr, &m_addr.sin_addr) == 0)
            {
                m_addr.sin_addr.s_addr = INADDR_ANY;
            }

            if (bind(m_socket, (struct sockaddr*)&m_addr, sizeof(m_addr)) < 0)
            {
                std::cerr << "Bind failed: " << strerror(errno) << std::endl;
                return -1;
            }

            struct timeval timeout;
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;

            if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
            {
                std::cerr << "setsockopt(SO_RCVTIMEO) failed: " << strerror(errno) << std::endl;
                return -1;
            }
        }

        return 0;
    }

    void Close()
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_socket != -1)
        {
            // SHUT_RDWR breaks the blocking recvfrom() immediately
            shutdown(m_socket, SHUT_RDWR);
            close(m_socket);
            m_socket = -1;
        }
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_socket != -1)
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
        // Allow ACKs on SUB sockets. Block only regular data.
        if (m_type == Type::SUB && header.GetId() != dmq::ACK_REMOTE_ID) {
            std::cerr << "Send operation not allowed on SUB socket." << std::endl;
            return -1;
        }

        if (m_sendTransport != this) {
            std::cerr << "Send operation not allowed (Receive only)." << std::endl;
            return -1;
        }

        // Get payload data without string copy if possible
        // Note: os.str() still creates a copy. In a full zero-copy version,
        // we would use a custom streambuf that points to m_sendBuffer.
        auto payload = os.str();
        uint16_t payloadLen = static_cast<uint16_t>(payload.length());
        
        if (payloadLen > (BUFFER_SIZE - DmqHeader::HEADER_SIZE)) {
            std::cerr << "Error: Payload too large for static buffer." << std::endl;
            return -1;
        }

        // Prepare header values in Network Byte Order
        uint16_t marker = htons(header.GetMarker());
        uint16_t id     = htons(header.GetId());
        uint16_t seqNum = htons(header.GetSeqNum());
        uint16_t length = htons(payloadLen);

        // Copy header to start of send buffer
        memcpy(m_sendBuffer, &marker, 2);
        memcpy(m_sendBuffer + 2, &id, 2);
        memcpy(m_sendBuffer + 4, &seqNum, 2);
        memcpy(m_sendBuffer + 6, &length, 2);

        // Copy payload after header
        if (payloadLen > 0) {
            memcpy(m_sendBuffer + DmqHeader::HEADER_SIZE, payload.data(), payloadLen);
        }

        size_t totalSize = DmqHeader::HEADER_SIZE + payloadLen;

        ssize_t sent = sendto(m_socket, m_sendBuffer, totalSize, 0,
            (struct sockaddr*)&m_addr, sizeof(m_addr));
            
        if (sent != (ssize_t)totalSize) return -1;

        // Always track the message (unless it is an ACK)
        if (header.GetId() != dmq::ACK_REMOTE_ID && m_transportMonitor) {
            if (m_transportMonitor->Add(header.GetSeqNum(), header.GetId()) == false) {
                return -1;
            }
        }
        return 0;
    }

    virtual int Receive(xstringstream& is, DmqHeader& header) override
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_recvTransport != this) {
            std::cerr << "Receive operation not allowed (Send only)." << std::endl;
            return -1;
        }

        sockaddr_in fromAddr;
        socklen_t addrLen = sizeof(fromAddr);
        ssize_t size = recvfrom(m_socket, m_buffer, sizeof(m_buffer), 0,
            (struct sockaddr*)&fromAddr, &addrLen);

        if (size < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                return -1; // Timeout
            return -1;
        }

        if (size < DmqHeader::HEADER_SIZE) {
            return -1;
        }

        // 1. Read Header directly from m_buffer (Network Byte Order)
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
            return -1;
        }

        // 2. Write payload to provided stream
        uint16_t payloadSize = header.GetLength();
        if (payloadSize > (size - DmqHeader::HEADER_SIZE))
            payloadSize = static_cast<uint16_t>(size - DmqHeader::HEADER_SIZE);

        if (payloadSize > 0) {
            is.write(m_buffer + DmqHeader::HEADER_SIZE, payloadSize);
        }

        // 3. Handle ACKs and Metadata
        if (header.GetId() == dmq::ACK_REMOTE_ID)
        {
            if (m_transportMonitor)
                m_transportMonitor->Remove(header.GetSeqNum());
        }
        else if (m_transportMonitor && m_sendTransport)
        {
            // Send ACK using a small stack buffer to avoid any heap
            uint16_t a_marker = htons(DmqHeader::MARKER);
            uint16_t a_id     = htons(dmq::ACK_REMOTE_ID);
            uint16_t a_seqNum = htons(header.GetSeqNum());
            uint16_t a_length = 0;

            char ackBuf[DmqHeader::HEADER_SIZE];
            memcpy(ackBuf, &a_marker, 2);
            memcpy(ackBuf + 2, &a_id, 2);
            memcpy(ackBuf + 4, &a_seqNum, 2);
            memcpy(ackBuf + 6, &a_length, 2);

            // Use the subscriber's m_addr which was set by recvfrom if m_type == SUB
            sockaddr_in targetAddr = (m_type == Type::SUB) ? fromAddr : m_addr;

            sendto(m_socket, ackBuf, DmqHeader::HEADER_SIZE, 0,
                (struct sockaddr*)&targetAddr, sizeof(targetAddr));
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

    /// @note UDP datagrams larger than the network MTU (typically 1500 bytes) will be 
    /// fragmented by the IP layer. If any fragment is lost, the entire message is 
    /// discarded. For maximum reliability, keep serialized messages under 1400 bytes.
    /// Messages exceeding BUFFER_SIZE will be truncated and discarded by the OS.
    static const int BUFFER_SIZE = 4096;
    char m_buffer[BUFFER_SIZE] = { 0 };
    char m_sendBuffer[BUFFER_SIZE] = { 0 };
    dmq::RecursiveMutex m_mutex;
};

using UdpTransport = LinuxUdpTransport;

} // namespace dmq::transport

#endif // LINUX_UDP_TRANSPORT_H