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
#include "port/transport/common/ITransport.h"
#include "port/transport/common/DmqHeader.h"
#include "port/transport/common/ITransportMonitor.h"

// lwIP Includes
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/sys.h"
#include "lwip/api.h"

#include <cstring>
#include <cstdlib>
#include <cerrno> 

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
        const dmq::LockGuard<dmq::RecursiveMutex> recvLock(m_recvMutex);
        const dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        Close();    // a second Create() replaces the socket instead of leaking it
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

            // Short timeout so polling this socket for ACKs doesn't hang.
            struct timeval timeout = ToTimeval(dmq::UDP_PUB_RECV_TIMEOUT);

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

            // Receive timeout, so the receive thread can check for exit
            struct timeval timeout = ToTimeval(dmq::UDP_SUB_RECV_TIMEOUT);

            if (lwip_setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
            {
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
            lwip_close(m_socket);
            m_socket = -1;
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

        // Convert header fields to Network Byte Order (Big Endian) 
        DmqHeader wireHeader = header;
        wireHeader.SetLength(payloadLen);
        wireHeader.Encode(m_sendBuffer);

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
        // m_recvMutex, not m_mutex: a Send() on another thread (e.g. a PUB socket
        // that also drains its ACKs here) isn't held up for the receive timeout.
        const dmq::LockGuard<dmq::RecursiveMutex> recvLock(m_recvMutex);
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
                return RECEIVE_NO_DATA;

            // Real socket error
            return -1;
        }

        if (static_cast<size_t>(size) < DmqHeader::HEADER_SIZE) {  // size >= 0 here (checked above)
            return -1;
        }

        // 1. Read Header (Network Byte Order)
        header.Decode(m_buffer);

        if (!header.IsValid())
        {
            return -1; // Sync marker mismatch
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

}

#endif // ARM_LWIP_UDP_TRANSPORT_H