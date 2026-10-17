#ifndef LINUX_UDS_TRANSPORT_H
#define LINUX_UDS_TRANSPORT_H

/// @file LinuxUdsTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Linux Unix domain socket transport implementation for DelegateMQ.
///
/// @details
/// Local inter-process communication over a Unix domain datagram socket
/// (`AF_UNIX`, `SOCK_DGRAM`). This is the Linux counterpart to
/// `Win32PipeTransport`: one transport instance is duplex, so a single
/// participant can both send and receive over it.
///
/// Each endpoint is a filesystem path. Every instance binds its own local path
/// (so the peer can address it) and sends to the peer's path:
///
/// @code
/// // Process A                                     // Process B
/// t.Create(Type::SUB, "/tmp/a.sock", "/tmp/b.sock"); t.Create(Type::PUB, "/tmp/b.sock", "/tmp/a.sock");
/// @endcode
///
/// Key Features:
/// 1. **No start order**: datagram sockets have no connection. Either side may
///    start first; a Send() to a peer that isn't bound yet fails (returns -1)
///    and the next Send() after the peer binds succeeds.
/// 2. **Message boundaries**: one Send() is exactly one Receive().
///    Linux Unix datagram sockets are reliable and ordered, so no ACK/retry
///    layer is used (same as `Win32PipeTransport`).
/// 3. **Non-blocking by default**: Receive() returns -1 immediately when no
///    datagram is waiting, matching `Win32PipeTransport`'s `PIPE_NOWAIT` mode.
///    SetRecvTimeout() makes Receive() block up to the given time instead.
///    Receive() does not hold the send lock while waiting, so a concurrent
///    Send() on the same instance is never held up by an idle read.
/// 4. **Stale paths**: a crashed process leaves its socket file behind. Create()
///    unlinks the local path before bind(), and Close() unlinks it again.
///
/// Type is kept for API parity with `Win32PipeTransport` (`SUB` = server end,
/// `PUB` = client end). Both ends are duplex. The one difference: a `SUB`
/// created with `peerPath == nullptr` replies to whoever sent it the most recent
/// datagram, instead of a fixed peer.
///
/// @note This class is specific to Linux and uses POSIX socket APIs.

#include "delegate/DelegateOpt.h"
#include "port/transport/common/ITransport.h"
#include "port/transport/common/DmqHeader.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <errno.h>
#include <mutex>
#include <chrono>

namespace dmq::transport {

/// @brief Linux Unix domain datagram socket transport.
class LinuxUdsTransport : public ITransport
{
    XALLOCATOR
public:
    enum class Type
    {
        PUB,
        SUB
    };

    LinuxUdsTransport() = default;

    ~LinuxUdsTransport()
    {
        Close();
    }

    /// Create the socket and bind it to localPath.
    /// @param[in] type PUB (client end) or SUB (server end). Both are duplex.
    /// @param[in] localPath This endpoint's socket path. Required.
    /// @param[in] peerPath The other endpoint's socket path. Required for PUB;
    ///   optional for SUB (nullptr = reply to the most recent sender).
    /// @return 0 on success, -1 on failure.
    int Create(Type type, const char* localPath, const char* peerPath)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_type = type;

        if (localPath == nullptr || (type == Type::PUB && peerPath == nullptr))
        {
            std::cerr << "LinuxUdsTransport: missing socket path." << std::endl;
            return -1;
        }

        if (!MakeAddr(localPath, m_localAddr) ||
            (peerPath && !MakeAddr(peerPath, m_peerAddr)))
        {
            std::cerr << "LinuxUdsTransport: socket path too long (max "
                      << sizeof(m_localAddr.sun_path) - 1 << " chars)." << std::endl;
            return -1;
        }
        m_fixedPeer = (peerPath != nullptr);
        m_hasPeer = m_fixedPeer;

        m_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (m_socket < 0)
        {
            std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
            return -1;
        }

        // A crashed run leaves the socket file behind, which makes bind() fail.
        unlink(m_localAddr.sun_path);
        if (bind(m_socket, (struct sockaddr*)&m_localAddr, sizeof(m_localAddr)) < 0)
        {
            std::cerr << "Bind failed (" << m_localAddr.sun_path << "): " << strerror(errno) << std::endl;
            close(m_socket);
            m_socket = -1;
            return -1;
        }
        m_bound = true;
        return 0;
    }

    void Close()
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_socket != -1)
        {
            shutdown(m_socket, SHUT_RDWR);
            close(m_socket);
            m_socket = -1;
        }
        if (m_bound)
        {
            unlink(m_localAddr.sun_path);
            m_bound = false;
        }
    }

    /// Make Receive() wait up to timeout for a datagram. 0 (default) = non-blocking.
    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_recvTimeout = timeout;
    }

    virtual int Send(dmq::xostringstream& os, const DmqHeader& header) override
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (os.bad() || os.fail())
            return -1;

        if (m_socket == -1 || !m_hasPeer)
            return -1;

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

        // MSG_DONTWAIT: if the peer isn't reading and its queue is full, fail
        // this send rather than stall the caller. A peer that isn't bound yet
        // fails with ENOENT/ECONNREFUSED.
        ssize_t sent = sendto(m_socket, m_sendBuffer, totalSize, MSG_DONTWAIT,
            (struct sockaddr*)&m_peerAddr, sizeof(m_peerAddr));

        if (sent != (ssize_t)totalSize)
            return -1;

        return 0;
    }

    virtual int Receive(dmq::xstringstream& is, DmqHeader& header) override
    {
        // Receive-side state (m_buffer) is only touched here, under m_recvMutex.
        // m_mutex is not held while waiting, so Send() is never blocked by an idle read.
        const std::lock_guard<dmq::RecursiveMutex> recvLock(m_recvMutex);

        int sock;
        std::chrono::milliseconds timeout;
        {
            const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
            sock = m_socket;
            timeout = m_recvTimeout;
        }
        if (sock == -1)
            return -1;

        if (timeout.count() > 0)
        {
            pollfd pfd{ sock, POLLIN, 0 };
            if (poll(&pfd, 1, static_cast<int>(timeout.count())) <= 0)
                return -1;  // timeout or error
        }

        sockaddr_un fromAddr{};
        socklen_t addrLen = sizeof(fromAddr);
        ssize_t size = recvfrom(sock, m_buffer, sizeof(m_buffer), MSG_DONTWAIT,
            (struct sockaddr*)&fromAddr, &addrLen);

        if (size < static_cast<ssize_t>(DmqHeader::HEADER_SIZE))
            return -1;  // no data (EAGAIN), error, or runt datagram

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
            return -1;

        // 2. Write payload to provided stream
        uint16_t payloadSize = header.GetLength();
        if (payloadSize > (size - DmqHeader::HEADER_SIZE))
            payloadSize = static_cast<uint16_t>(size - DmqHeader::HEADER_SIZE);

        if (payloadSize > 0) {
            is.write(m_buffer + DmqHeader::HEADER_SIZE, payloadSize);
        }

        // 3. A SUB without a fixed peer replies to the most recent (bound) sender.
        if (m_type == Type::SUB && addrLen > sizeof(sa_family_t))
        {
            const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
            if (!m_fixedPeer)
            {
                m_peerAddr = fromAddr;
                m_hasPeer = true;
            }
        }

        return 0;
    }

private:
    static bool MakeAddr(const char* path, sockaddr_un& addr)
    {
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        if (strlen(path) >= sizeof(addr.sun_path))
            return false;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        return true;
    }

    int m_socket = -1;
    bool m_bound = false;
    bool m_hasPeer = false;
    bool m_fixedPeer = false;   // peer given to Create(); never overwritten by Receive()
    sockaddr_un m_localAddr{};
    sockaddr_un m_peerAddr{};
    Type m_type = Type::PUB;
    std::chrono::milliseconds m_recvTimeout{ 0 };

    /// Max datagram size. Linux Unix datagram sockets accept datagrams up to
    /// the socket send buffer size (net.core.wmem_default, typically ~200 KB),
    /// so this static buffer is the practical limit.
    static const int BUFFER_SIZE = 65536;
    char m_buffer[BUFFER_SIZE] = { 0 };
    char m_sendBuffer[BUFFER_SIZE] = { 0 };
    dmq::RecursiveMutex m_mutex;      // socket state + send path
    dmq::RecursiveMutex m_recvMutex;  // receive path
};

} // namespace dmq::transport

#endif // LINUX_UDS_TRANSPORT_H
