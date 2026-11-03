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
/// * Enable IPv4: `CONFIG_NET_IPV4=y`
/// * A network interface: on `native_sim` the loopback interface
///   (`CONFIG_NET_LOOPBACK=y`, needs `CONFIG_NET_DRIVERS=y`) is enough for
///   same-process testing with no host-side setup -- see
///   `example/sample-projects/zephyr-udp-serializer/prj.conf`. A real
///   target instead needs its actual link-layer driver (Ethernet, Wi-Fi, etc.).
///
/// This file calls the `zsock_*`-prefixed socket functions (`zsock_socket()`,
/// `zsock_bind()`, `zsock_sendto()`, etc.) rather than the unprefixed BSD
/// names. Getting the unprefixed names requires `CONFIG_POSIX_API=y`, which
/// replaces large parts of the standard header set (`<sys/socket.h>`,
/// pthread types, ...) with Zephyr's own POSIX compatibility layer --
/// something that collides with the *host's* C++ standard library headers
/// when building for `native_sim` (which links against the host's real
/// libstdc++), and is unnecessary weight on a real target that has no such
/// conflict. `CONFIG_NET_SOCKETS_POSIX_NAMES`, an older, narrower way to get
/// just the unprefixed socket names without the rest of `CONFIG_POSIX_API`,
/// was deprecated in Zephyr 3.7 and removed -- `zsock_*` is the current,
/// portable choice regardless of target.
/// 
/// **Key Features:**
/// 1. **Direct Execution**: Executes network operations directly on the calling thread,
///    avoiding context switch overhead and preventing deadlocks.
/// 2. **Zero-Copy Friendly**: Uses the standard BSD API which Zephyr optimizes internally.
/// 3. **Reliability**: Fully integrated with `TransportMonitor` for ACKs/Retries.
/// 4. **Endianness**: Uses `htons`/`ntohs` for standard network byte order compatibility.

#include "delegate/DelegateOpt.h"
#include "port/transport/common/ITransport.h"
#include "port/transport/common/DmqHeader.h"
#include "port/transport/common/ITransportMonitor.h"

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#include <cstring>
#include <chrono>
#include <cstdlib>
#include <cerrno>

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
        const dmq::LockGuard<dmq::RecursiveMutex> recvLock(m_recvMutex);
        const dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        Close();    // a second Create() replaces the socket instead of leaking it
        m_type = type;

        // Create UDP socket. zsock_* (not the bare BSD names) so this
        // compiles without CONFIG_POSIX_API -- see the class doc comment.
        m_socket = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
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
            if (zsock_inet_pton(AF_INET, addr, &m_addr.sin_addr) != 1)
            {
                // printk("Invalid IP address format.\n");
                Close();
                return -1;
            }

            // Short timeout so polling this socket for ACKs doesn't hang.
            struct timeval timeout = ToTimeval(dmq::UDP_PUB_RECV_TIMEOUT);

            if (zsock_setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
            {
                // printk("setsockopt(SO_RCVTIMEO) failed\n");
                Close();
                return -1;
            }
        }
        else if (type == Type::SUB)
        {
            m_addr.sin_addr.s_addr = INADDR_ANY;

            if (zsock_bind(m_socket, (struct sockaddr*)&m_addr, sizeof(m_addr)) < 0)
            {
                // printk("Bind failed: %d\n", errno);
                Close();
                return -1;
            }

            // Receive timeout, so the receive thread can check for exit
            struct timeval timeout = ToTimeval(dmq::UDP_SUB_RECV_TIMEOUT);

            if (zsock_setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
            {
                // printk("setsockopt(SO_RCVTIMEO) failed\n");
                Close();
                return -1;
            }
        }

        return 0;
    }

    /// @details Waits for a Receive() in progress on another thread to return
    /// (at most its receive timeout) before closing, so the socket is never
    /// closed under it.
    void Close()
    {
        const dmq::LockGuard<dmq::RecursiveMutex> recvLock(m_recvMutex);
        const dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_socket >= 0)
        {
            // Receive() has returned (m_recvMutex is held), so nothing is blocked on it
            zsock_shutdown(m_socket, ZSOCK_SHUT_RDWR);
            zsock_close(m_socket);
            m_socket = -1;
        }
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        const dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_socket >= 0)
        {
            struct timeval tv;
            tv.tv_sec = timeout.count() / 1000;
            tv.tv_usec = (timeout.count() % 1000) * 1000;
            zsock_setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
    }

    virtual int Send(xostringstream& os, const DmqHeader& header) override
    {
        const dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
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
        if (payload.length() > (BUFFER_SIZE - DmqHeader::HEADER_SIZE)) {
            return -1;
        }
        uint16_t payloadLen = static_cast<uint16_t>(payload.length());

        // Convert to Network Byte Order (Big Endian)
        DmqHeader wireHeader = header;
        wireHeader.SetLength(payloadLen);
        wireHeader.Encode(m_sendBuffer);

        if (payloadLen > 0) {
            memcpy(m_sendBuffer + DmqHeader::HEADER_SIZE, payload.data(), payloadLen);
        }

        size_t totalSize = DmqHeader::HEADER_SIZE + payloadLen;

        ssize_t sent = zsock_sendto(m_socket, m_sendBuffer, totalSize, 0,
            (struct sockaddr*)&m_addr, sizeof(m_addr));
        if (sent != (ssize_t)totalSize) return -1;

        return 0;
    }

    virtual int Receive(xstringstream& is, DmqHeader& header) override
    {
        // m_recvMutex, not m_mutex: a Send() on another thread (e.g. a PUB socket
        // that also drains its ACKs here) isn't held up for the receive timeout.
        const dmq::LockGuard<dmq::RecursiveMutex> recvLock(m_recvMutex);
        if (m_recvTransport != this || m_socket < 0) {
            return -1;
        }

        sockaddr_in fromAddr;
        socklen_t addrLen = sizeof(fromAddr);
        
        ssize_t size = zsock_recvfrom(m_socket, m_buffer, sizeof(m_buffer), 0,
            (struct sockaddr*)&fromAddr, &addrLen);

        if (size < 0)
        {
            // Zephyr uses standard errno values
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                return RECEIVE_NO_DATA;

            return -1;
        }

        if (static_cast<size_t>(size) < DmqHeader::HEADER_SIZE) {
            return -1;
        }

        // 1. Read Header (Network Byte Order)
        header.Decode(m_buffer);

        if (!header.IsValid())
        {
            return -1; // Invalid marker
        }

        // 2. Extract Payload. One datagram is one message, so the header length
        // must match exactly; anything else (including a datagram cut short by
        // the buffer) is malformed and must not be delivered or ACKed.
        uint16_t payloadSize = header.GetLength();
        if (payloadSize != static_cast<size_t>(size) - DmqHeader::HEADER_SIZE)
            return -1;

        // Update m_addr to the sender's address so we can ACK back. Only after
        // validation, and under m_mutex since Send() uses it.
        if (m_type == Type::SUB) {
            const dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
            m_addr = fromAddr;
        }

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
        // ACK whenever a send path exists, whether or not this transport also
        // tracks its own sends: a receive-only transport (e.g. NetworkNode's SUB)
        // has no monitor but must still ACK the sender's RELIABLE messages.
        else if (m_sendTransport)
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

    static struct timeval ToTimeval(std::chrono::milliseconds ms)
    {
        struct timeval tv;
        tv.tv_sec = static_cast<long>(ms.count() / 1000);
        tv.tv_usec = static_cast<long>((ms.count() % 1000) * 1000);
        return tv;
    }

    // Datagram buffer (header + payload): DMQ_UDP_BUFFER_SIZE, default the
    // 1500-byte Ethernet MTU.
    static constexpr size_t BUFFER_SIZE = dmq::UDP_BUFFER_SIZE;
    char m_buffer[BUFFER_SIZE] = { 0 };
    char m_sendBuffer[BUFFER_SIZE] = { 0 };
    dmq::RecursiveMutex m_mutex;        ///< Socket open/close and Send() state (m_addr)
    dmq::RecursiveMutex m_recvMutex;    ///< Held by Receive(); taken before m_mutex
};

/// @brief Backward-compatible name: application code (e.g. the sender.h/
/// receiver.h pattern shared by every *-udp-serializer sample) references
/// dmq::transport::UdpTransport generically and stays portable across
/// whichever DMQ_TRANSPORT_* was selected at compile time, the same way
/// dmq::os::Thread works across DMQ_THREAD_* -- see LinuxUdpTransport.h/
/// Win32UdpTransport.h for the other two ports that already do this.
using UdpTransport = ZephyrUdpTransport;

}

#endif // ZEPHYR_UDP_TRANSPORT_H