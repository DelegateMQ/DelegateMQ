#ifndef LINUX_TCP_TRANSPORT_H
#define LINUX_TCP_TRANSPORT_H

/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief Linux TCP transport implementation for DelegateMQ.
/// 
/// @details
/// This class implements the ITransport interface using standard Linux BSD sockets. 
/// It supports both CLIENT and SERVER modes for reliable, stream-based communication.
/// In SERVER mode, it supports multiple simultaneous client connections and broadcasts
/// outgoing messages to all connected participants.
/// 
/// Key Features:
/// 1. **Thread-Safe I/O**: Executes socket operations directly on the calling thread, 
///    relying on OS-level thread safety for concurrent Send/Receive operations.
/// 2. **Low Latency**: Configures `TCP_NODELAY` to disable Nagle's algorithm, optimized 
///    for the small, frequent packets typical of RPC/delegate calls.
/// 3. **Non-Blocking Poll**: Utilizes `select()` in the receive loop to prevent 
///    thread blocking when no data is available, facilitating clean shutdowns.
/// 4. **Multi-Client Multiplexing**: SERVER mode manages a list of clients and 
///    polls them for data using `select()`.
/// 5. **Reliability**: Fully integrated with `TransportMonitor` to handle sequence 
///    tracking and ACK generation.
/// 
/// @note This class is specific to Linux and uses POSIX socket APIs.

#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <vector>
#include <algorithm>

#include "delegate/DelegateOpt.h"
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include "port/transport/ITransportMonitor.h"

namespace dmq::transport {

/// @brief A TCP transport implementation for Linux using BSD sockets.
class LinuxTcpTransport : public ITransport
{
public:
    enum class Type { SERVER, CLIENT };

    LinuxTcpTransport() : m_sendTransport(this), m_recvTransport(this)
    {
    }

    ~LinuxTcpTransport() { Close(); }

    /// @brief Create a TCP transport.
    /// @param type SERVER or CLIENT.
    /// @param addr The IP address string.
    /// @param port The TCP port.
    /// @return 0 on success, -1 on failure.
    int Create(Type type, const char* addr, uint16_t port)
    {
        m_type = type;
        m_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_socket < 0) return -1;

        // Set TCP_NODELAY to disable Nagle's algorithm for low-latency RPC
        int flag = 1;
        setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

        sockaddr_in srv_addr{};
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_port = htons(port);
        inet_aton(addr, &srv_addr.sin_addr);

        if (type == Type::SERVER) {
            int opt = 1;
            setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            if (bind(m_socket, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) return -1;
            if (listen(m_socket, 5) < 0) return -1;
        }
        else {
            if (connect(m_socket, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) return -1;
            m_connFd = m_socket;

            struct timeval tv;
            tv.tv_sec = 2; tv.tv_usec = 0;
            setsockopt(m_connFd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        }

        return 0;
    }

    /// @brief Close all sockets and clean up.
    void Close()
    {
        // Close Connected Socket
        if (m_connFd >= 0) 
        {
            shutdown(m_connFd, SHUT_RDWR);
            if (m_connFd != m_socket) 
            {
                close(m_connFd);
            }
        }

        // Close all server-accepted clients
        for (auto s : m_serverClients) {
            shutdown(s, SHUT_RDWR);
            close(s);
        }
        m_serverClients.clear();

        // Close Listen Socket
        if (m_socket >= 0) 
        {
            shutdown(m_socket, SHUT_RDWR);
            close(m_socket);
        }

        m_connFd = m_socket = -1;
    }

    /// @brief Set the receive timeout for all active sockets.
    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        m_recvTimeout = timeout;
        struct timeval tv;
        tv.tv_sec = timeout.count() / 1000;
        tv.tv_usec = (timeout.count() % 1000) * 1000;

        if (m_connFd >= 0)
        {
            setsockopt(m_connFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
        for (auto s : m_serverClients) {
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
    }

    /// @brief Send data over the TCP link.
    /// @details In SERVER mode, this broadcasts to all connected clients.
    virtual int Send(xostringstream& os, const DmqHeader& header) override
    {
        if (m_type == Type::CLIENT) {
            return SendToSocket(m_connFd, os, header);
        } else {
            int lastErr = 0;
            for (auto s : m_serverClients) {
                if (SendToSocket(s, os, header) != 0) lastErr = -1;
            }
            return lastErr;
        }
    }

    /// @brief Receive data from the TCP link.
    virtual int Receive(xstringstream& is, DmqHeader& header) override
    {
        if (m_type == Type::SERVER) {
            return ReceiveServer(is, header);
        } else {
            return ReceiveSocket(m_connFd, is, header);
        }
    }

    void SetTransportMonitor(ITransportMonitor* tm) {
        m_transportMonitor = tm;
    }
    void SetSendTransport(ITransport* st) {
        m_sendTransport = st;
    }
    void SetRecvTransport(ITransport* rt) {
        m_recvTransport = rt;
    }

private:
    /// @brief Internal helper to send to a specific socket.
    int SendToSocket(int fd, xostringstream& os, const DmqHeader& header) {
        if (fd < 0) return -1;

        auto payload = os.str();
        DmqHeader headerCopy = header;
        headerCopy.SetLength(static_cast<uint16_t>(payload.length()));

        xostringstream ss(std::ios::binary);
        
        // Convert to Network Byte Order (Big Endian)
        uint16_t marker = htons(headerCopy.GetMarker());
        uint16_t id     = htons(headerCopy.GetId());
        uint16_t seqNum = htons(headerCopy.GetSeqNum());
        uint16_t length = htons(headerCopy.GetLength());

        ss.write((char*)&marker, 2);
        ss.write((char*)&id, 2);
        ss.write((char*)&seqNum, 2);
        ss.write((char*)&length, 2);
        ss.write(payload.data(), payload.size());

        auto packet = ss.str();

        ssize_t sent = write(fd, packet.data(), packet.size());
        if (sent != (ssize_t)packet.size()) return -1;

        if (headerCopy.GetId() != dmq::ACK_REMOTE_ID && m_transportMonitor)
            m_transportMonitor->Add(headerCopy.GetSeqNum(), headerCopy.GetId());

        return 0;
    }

    /// @brief Internal helper to handle server-side multiplexing.
    int ReceiveServer(xstringstream& is, DmqHeader& header) {
        // 1. Lazy Accept: Check for new client connections
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(m_socket, &fds);
        struct timeval tv = { 0, 1000 }; // 1ms poll

        if (select(m_socket + 1, &fds, nullptr, nullptr, &tv) > 0) {
            int client = accept(m_socket, nullptr, nullptr);
            if (client >= 0) {
                struct timeval t;
                t.tv_sec = m_recvTimeout.count() / 1000;
                t.tv_usec = (m_recvTimeout.count() % 1000) * 1000;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
                m_serverClients.push_back(client);
            }
        }

        // 2. Poll all connected clients for data
        if (m_serverClients.empty()) return -1;

        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = -1;
        for (auto s : m_serverClients) {
            FD_SET(s, &readfds);
            if (s > max_fd) max_fd = s;
        }

        tv.tv_usec = 1000; // 1ms poll
        if (select(max_fd + 1, &readfds, nullptr, nullptr, &tv) > 0) {
            for (auto it = m_serverClients.begin(); it != m_serverClients.end(); ) {
                if (FD_ISSET(*it, &readfds)) {
                    int result = ReceiveSocket(*it, is, header);
                    if (result == -2) { // Disconnected
                        close(*it);
                        it = m_serverClients.erase(it);
                        continue; 
                    }
                    return result;
                }
                ++it;
            }
        }
        return -1;
    }

    /// @brief Internal helper to read from a specific socket and parse DMQ protocol.
    int ReceiveSocket(int fd, xstringstream& is, DmqHeader& header) {
        if (fd < 0) return -1;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval tv = { 0, 1000 };
        if (select(fd + 1, &readfds, nullptr, nullptr, &tv) <= 0) return -1;

        // 1. Read Header
        char headerBuf[DmqHeader::HEADER_SIZE];
        if (!ReadExact(fd, headerBuf, DmqHeader::HEADER_SIZE)) return -2; // Disconnected

        xstringstream ss(std::ios::in | std::ios::out | std::ios::binary);
        ss.write(headerBuf, DmqHeader::HEADER_SIZE);
        ss.seekg(0);

        uint16_t val;

        // Read Marker (Convert Network -> Host)
        ss.read((char*)&val, 2); header.SetMarker(ntohs(val));
        if (header.GetMarker() != DmqHeader::MARKER) return -1;

        ss.read((char*)&val, 2); header.SetId(ntohs(val));
        ss.read((char*)&val, 2); header.SetSeqNum(ntohs(val));
        ss.read((char*)&val, 2); header.SetLength(ntohs(val));

        // 2. Read Payload
        uint16_t length = header.GetLength();
        if (length > 0) {
            m_recvBuf.resize(length);
            if (!ReadExact(fd, m_recvBuf.data(), length)) return -2;
            is.write(m_recvBuf.data(), length);
        }

        // 3. Handle Acknowledgment
        if (header.GetId() == dmq::ACK_REMOTE_ID) {
            if (m_transportMonitor) m_transportMonitor->Remove(header.GetSeqNum());
        }
        else if (m_sendTransport) {
            xostringstream ss_ack;
            DmqHeader ack;
            ack.SetId(dmq::ACK_REMOTE_ID);
            ack.SetSeqNum(header.GetSeqNum());
            m_sendTransport->Send(ss_ack, ack);
        }
        return 0;
    }

    /// @brief Read exactly 'len' bytes from the socket.
    bool ReadExact(int fd, char* buf, size_t len)
    {
        size_t total = 0;
        while (total < len) {
            ssize_t r = read(fd, buf + total, len - total);
            if (r <= 0) return false;
            total += r;
        }
        return true;
    }

    int m_socket = -1;
    int m_connFd = -1;
    std::vector<int> m_serverClients;
    std::vector<char> m_recvBuf;
    std::chrono::milliseconds m_recvTimeout{2000};
    Type m_type = Type::SERVER;
    
    ITransport* m_sendTransport, * m_recvTransport;
    ITransportMonitor* m_transportMonitor = nullptr;
};

using TcpTransport = LinuxTcpTransport;

} // namespace dmq::transport

#endif // LINUX_TCP_TRANSPORT_H