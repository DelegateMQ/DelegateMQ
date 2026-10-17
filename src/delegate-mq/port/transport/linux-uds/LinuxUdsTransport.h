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
///    probes the local path first: a stale socket (nothing listening) is
///    unlinked and reused; a path a live process is still bound to makes
///    Create() fail rather than take it over. Close() unlinks the path again.
///
/// @note Socket files are ordinary filesystem entries, so access is governed by
/// the directory's permissions. A world-writable directory such as `/tmp` lets
/// any local user send to the socket. Use a per-user directory such as
/// `$XDG_RUNTIME_DIR` when that matters.
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
#include <sys/stat.h>
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
        // Unlink it only if it is a stale socket; never steal a live one.
        if (!ClearStalePath(m_localAddr))
        {
            close(m_socket);
            m_socket = -1;
            return -1;
        }
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
        // Lock order matches Receive(): m_recvMutex, then m_mutex. Holding
        // m_recvMutex means no Receive() is mid-wait on the socket we close
        // (Close() waits at most one recv timeout for it to return).
        const std::lock_guard<dmq::RecursiveMutex> recvLock(m_recvMutex);
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
        if (payload.length() > (BUFFER_SIZE - DmqHeader::HEADER_SIZE)) {
            std::cerr << "Error: Payload too large for static buffer." << std::endl;
            return -1;
        }
        uint16_t payloadLen = static_cast<uint16_t>(payload.length());

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
        // The socket copied below stays valid: Close() also takes m_recvMutex.
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
        // MSG_TRUNC: return the datagram's real length, so an oversized one is
        // detected and dropped instead of being silently cut off.
        ssize_t size = recvfrom(sock, m_buffer, sizeof(m_buffer), MSG_DONTWAIT | MSG_TRUNC,
            (struct sockaddr*)&fromAddr, &addrLen);

        if (size < static_cast<ssize_t>(DmqHeader::HEADER_SIZE))
            return -1;  // no data (EAGAIN), error, or runt datagram
        if (size > static_cast<ssize_t>(sizeof(m_buffer)))
            return -1;  // truncated: larger than the receive buffer

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

        // 2. Write payload to provided stream. One Send() is one datagram, so
        // the header length must match exactly; anything else is malformed.
        uint16_t payloadSize = header.GetLength();
        if (payloadSize != (size - DmqHeader::HEADER_SIZE))
            return -1;

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
    /// Prepare addr's path for bind(). A connect() probe tells a stale socket
    /// file (ECONNREFUSED: nothing bound) from a live one (connect succeeds).
    /// Only a stale socket is unlinked; a live one, or anything that isn't a
    /// socket, is left alone.
    /// @return true if bind() may proceed.
    static bool ClearStalePath(const sockaddr_un& addr)
    {
        // connect() also reports ECONNREFUSED for a regular file, so check the
        // file type first. lstat(): a symlink is not followed or removed.
        struct stat st{};
        if (lstat(addr.sun_path, &st) != 0)
            return true;    // nothing there
        if (!S_ISSOCK(st.st_mode))
            return true;    // not a socket: leave it; bind() fails and reports it

        int probe = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (probe < 0)
        {
            std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
            return false;
        }
        int rc = connect(probe, (const struct sockaddr*)&addr, sizeof(addr));
        int err = errno;
        close(probe);

        if (rc == 0)
        {
            std::cerr << "LinuxUdsTransport: " << addr.sun_path
                      << " is in use by another process." << std::endl;
            return false;
        }
        if (err == ECONNREFUSED)
            unlink(addr.sun_path);  // stale socket file
        // Any other error (e.g. EACCES): leave the file alone and let bind()
        // fail and report it.
        return true;
    }

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
