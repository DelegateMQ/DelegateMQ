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
        if (os.bad() || os.fail()) {
            std::cerr << "Stream state error." << std::endl;
            return -1;
        }

        // Allow ACKs on SUB sockets. Block only regular data.
        if (m_type == Type::SUB && header.GetId() != dmq::ACK_REMOTE_ID) {
            std::cerr << "Send operation not allowed on SUB socket." << std::endl;
            return -1;
        }

        if (m_sendTransport != this) {
            std::cerr << "Send operation not allowed (Receive only)." << std::endl;
            return -1;
        }

        // Create a local copy so we can modify the length
        DmqHeader headerCopy = header;

        // Calculate payload size and set it
        auto payload = os.str();
        if (payload.length() > UINT16_MAX) {
            std::cerr << "Error: Payload too large." << std::endl;
            return -1;
        }
        headerCopy.SetLength(static_cast<uint16_t>(payload.length()));

        xostringstream ss(std::ios::in | std::ios::out | std::ios::binary);

        // Convert to Network Byte Order (Big Endian)
        uint16_t marker = htons(headerCopy.GetMarker());
        uint16_t id     = htons(headerCopy.GetId());
        uint16_t seqNum = htons(headerCopy.GetSeqNum());
        uint16_t length = htons(headerCopy.GetLength());

        ss.write(reinterpret_cast<const char*>(&marker), sizeof(marker));
        ss.write(reinterpret_cast<const char*>(&id), sizeof(id));
        ss.write(reinterpret_cast<const char*>(&seqNum), sizeof(seqNum));
        ss.write(reinterpret_cast<const char*>(&length), sizeof(length));

        // Append Payload
        ss.write(payload.data(), payload.size());

        auto data = ss.str();

        ssize_t sent = sendto(m_socket, data.c_str(), data.size(), 0,
            (struct sockaddr*)&m_addr, sizeof(m_addr));
        if (sent != (ssize_t)data.size()) return -1;

        // Always track the message (unless it is an ACK)
        // Use Host Byte Order for ID check
        if (headerCopy.GetId() != dmq::ACK_REMOTE_ID && m_transportMonitor)
            m_transportMonitor->Add(headerCopy.GetSeqNum(), headerCopy.GetId());

        return 0;
    }

    virtual int Receive(xstringstream& is, DmqHeader& header) override
    {
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
            // std::cerr << "recvfrom failed: " << strerror(errno) << std::endl;
            return -1;
        }

        // Important: Update m_addr to the sender's address so we can ACK back
        // Note: For a true 1-to-N PUB/SUB, you might not want to overwrite m_addr permanently,
        // but for a 1-to-1 reliable link, this is required to route the ACK.
        if (m_type == Type::SUB) {
            m_addr = fromAddr;
        }

        xstringstream headerStream(std::ios::in | std::ios::out | std::ios::binary);
        headerStream.write(m_buffer, size);
        headerStream.seekg(0);

        uint16_t val = 0;

        // 1. Read Marker (Convert Network -> Host)
        headerStream.read(reinterpret_cast<char*>(&val), sizeof(val));
        header.SetMarker(ntohs(val));

        if (header.GetMarker() != DmqHeader::MARKER)
        {
            std::cerr << "Invalid sync marker!" << std::endl;
            return -1;
        }

        // 2. Read ID
        headerStream.read(reinterpret_cast<char*>(&val), sizeof(val));
        header.SetId(ntohs(val));

        // 3. Read SeqNum
        headerStream.read(reinterpret_cast<char*>(&val), sizeof(val));
        header.SetSeqNum(ntohs(val));

        // 4. Read Length
        headerStream.read(reinterpret_cast<char*>(&val), sizeof(val));
        header.SetLength(ntohs(val));

        is.write(m_buffer + DmqHeader::HEADER_SIZE, size - DmqHeader::HEADER_SIZE);

        // Get Host Byte Order values for logic check
        uint16_t id = header.GetId();
        uint16_t seqNum = header.GetSeqNum();

        if (id == dmq::ACK_REMOTE_ID)
        {
            if (m_transportMonitor)
                m_transportMonitor->Remove(seqNum);
        }
        else if (m_transportMonitor && m_sendTransport)
        {
            xostringstream ss_ack;
            DmqHeader ack;
            ack.SetId(dmq::ACK_REMOTE_ID);
            ack.SetSeqNum(seqNum);
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

    /// @note UDP datagrams larger than the network MTU (typically 1500 bytes) will be 
    /// fragmented by the IP layer. If any fragment is lost, the entire message is 
    /// discarded. For maximum reliability, keep serialized messages under 1400 bytes.
    /// Messages exceeding BUFFER_SIZE will be truncated and discarded by the OS.
    static const int BUFFER_SIZE = 4096;
    char m_buffer[BUFFER_SIZE] = { 0 };
};

using UdpTransport = LinuxUdpTransport;

} // namespace dmq::transport

#endif // LINUX_UDP_TRANSPORT_H