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
#include <mutex>
#include <thread>

#include "delegate/DelegateOpt.h"
#include "port/transport/common/ITransport.h"
#include "port/transport/common/DmqHeader.h"
#include "port/transport/common/ITransportMonitor.h"

namespace dmq::transport {

/// @brief A TCP transport implementation for Linux using BSD sockets.
class LinuxTcpTransport : public ITransport
{
    XALLOCATOR
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
        Close();    // a second Create() replaces the sockets instead of leaking them
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
            if (bind(m_socket, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) { Close(); return -1; }
            if (listen(m_socket, 5) < 0) { Close(); return -1; }
        }
        else {
            if (connect(m_socket, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) { Close(); return -1; }
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
        // m_mutex: Receive() may close the client socket on another thread
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

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
        {
            for (auto s : m_serverClients) {
                shutdown(s, SHUT_RDWR);
                close(s);
            }
            m_serverClients.clear();
        }

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
        
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        for (auto s : m_serverClients) {
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
    }

    /// @brief Send data over the TCP link.
    /// @details In SERVER mode, this broadcasts to all connected clients.
    virtual int Send(xostringstream& os, const DmqHeader& header) override
    {
        // m_mutex guards m_sendBuffer and keeps frames from different threads
        // (including ACKs written by Receive()) from interleaving on the stream.
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_type == Type::CLIENT) {
            return SendToSocket(m_connFd, os, header);
        } else {
            int lastErr = 0;
            for (auto fd : m_serverClients) {
                if (SendToSocket(fd, os, header) != 0) lastErr = -1;
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
            if (m_connFd < 0) {
                // The connection was dropped (see below). Wait out one receive
                // timeout before reporting it again, so a caller polling in a
                // tight loop (RemoteDispatcher's receive thread) doesn't spin.
                std::this_thread::sleep_for(m_recvTimeout);
                return -1;
            }
            int result = ReceiveSocket(m_connFd, is, header);
            if (result == -2) {
                // Disconnected, or the stream lost framing (bad header, partial
                // frame). Either way nothing more on it can be trusted. Close it:
                // a shut-down but still open socket stays readable at EOF, so the
                // next select() would return at once and the caller would spin.
                const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
                if (m_connFd >= 0) {
                    shutdown(m_connFd, SHUT_RDWR);
                    close(m_connFd);
                    m_connFd = m_socket = -1;
                }
            }
            return result;
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

        // Get payload data without string copy if possible
        auto payload = os.str();
        if (payload.length() > (BUFFER_SIZE - DmqHeader::HEADER_SIZE)) {
            std::cerr << "Error: Payload too large for static buffer." << std::endl;
            return -1;
        }
        uint16_t payloadLen = static_cast<uint16_t>(payload.length());

        // Prepare header values in Network Byte Order
        DmqHeader wireHeader = header;
        wireHeader.SetLength(payloadLen);
        wireHeader.Encode(m_sendBuffer);

        // Copy payload after header
        if (payloadLen > 0) {
            memcpy(m_sendBuffer + DmqHeader::HEADER_SIZE, payload.data(), payloadLen);
        }

        size_t totalSize = DmqHeader::HEADER_SIZE + payloadLen;

        return WriteAll(fd, m_sendBuffer, totalSize) ? 0 : -1;
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
                const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
                m_serverClients.push_back(client);
            }
        }

        // 2. Poll all connected clients for data
        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = -1;
        {
            const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
            if (m_serverClients.empty()) return RECEIVE_NO_DATA;
            for (auto s : m_serverClients) {
                FD_SET(s, &readfds);
                if (s > max_fd) max_fd = s;
            }
        }

        tv.tv_usec = 1000; // 1ms poll
        if (select(max_fd + 1, &readfds, nullptr, nullptr, &tv) > 0) {
            const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
            for (auto it = m_serverClients.begin(); it != m_serverClients.end(); ) {
                if (FD_ISSET(*it, &readfds)) {
                    int result = ReceiveSocket(*it, is, header);
                    if (result == -2) {
                        // Disconnected, or lost framing (bad header): drop this client.
                        close(*it);
                        it = m_serverClients.erase(it);
                        continue;
                    }
                    return result;
                }
                ++it;
            }
        }
        return RECEIVE_NO_DATA;
    }

    /// @brief Internal helper to read from a specific socket and parse DMQ protocol.
    int ReceiveSocket(int fd, xstringstream& is, DmqHeader& header) {
        if (fd < 0) return -1;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval tv = { 0, 1000 };
        int ready = select(fd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready == 0) return RECEIVE_NO_DATA;
        if (ready < 0) return -1;

        // 1. Read Header
        char headerBuf[DmqHeader::HEADER_SIZE];
        if (!ReadExact(fd, headerBuf, DmqHeader::HEADER_SIZE)) return -2; // Disconnected

        // Parse Header (Convert Network -> Host)
        header.Decode(headerBuf);

        // A stream has no resync point: after a bad header (or a payload we
        // can't hold and so can't skip), the next bytes aren't a header either.
        // Report it like a disconnect (-2) so the connection is dropped.
        if (!header.IsValid()) return -2;

        // 2. Read Payload
        uint16_t payloadSize = header.GetLength();
        if (payloadSize > 0) {
            if (payloadSize > BUFFER_SIZE) return -2;
            if (!ReadExact(fd, m_recvBuffer, payloadSize)) return -2;
            is.write(m_recvBuffer, payloadSize);
        }

        // 3. Handle Acknowledgment
        if (header.GetId() == dmq::ACK_REMOTE_ID) {
            if (m_transportMonitor) m_transportMonitor->Remove(header.GetSeqNum());
        }
        else if (m_sendTransport) {
            // Send ACK using a small stack buffer
            DmqHeader ack(dmq::ACK_REMOTE_ID, header.GetSeqNum());
            char ackBuf[DmqHeader::HEADER_SIZE];
            ack.Encode(ackBuf);

            const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);  // don't interleave with Send()
            (void)WriteAll(fd, ackBuf, DmqHeader::HEADER_SIZE);
        }
        return 0;
    }

    /// @brief Write all 'len' bytes; a short write would otherwise leave half a
    /// frame on the stream.
    bool WriteAll(int fd, const char* buf, size_t len)
    {
        size_t total = 0;
        while (total < len) {
            ssize_t w = write(fd, buf + total, len - total);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) return false;
            total += static_cast<size_t>(w);
        }
        return true;
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
    static const int BUFFER_SIZE = 4096;
    char m_recvBuffer[BUFFER_SIZE] = { 0 };
    char m_sendBuffer[BUFFER_SIZE] = { 0 };
    std::chrono::milliseconds m_recvTimeout{2000};
    Type m_type = Type::SERVER;
    
    ITransport* m_sendTransport, * m_recvTransport;
    ITransportMonitor* m_transportMonitor = nullptr;
    dmq::RecursiveMutex m_mutex;
};

using TcpTransport = LinuxTcpTransport;

} // namespace dmq::transport

#endif // LINUX_TCP_TRANSPORT_H
