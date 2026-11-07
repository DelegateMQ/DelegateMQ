#ifndef ARM_LWIP_NETCONN_UDP_TRANSPORT_H
#define ARM_LWIP_NETCONN_UDP_TRANSPORT_H

/// @file ArmLwipNetconnUdpTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
/// 
/// @brief ARM lwIP Netconn UDP transport implementation for DelegateMQ.
/// 
/// @details
/// This class implements the ITransport interface using the lwIP "Netconn" API.
/// This API is specific to lwIP+FreeRTOS and is more efficient than the BSD 
/// Socket API as it avoids the socket wrapper overhead.
/// 
/// Prerequisites:
/// - lwIP must be compiled with `LWIP_NETCONN=1`
/// - FreeRTOS must be running (Netconn relies on OS primitives)

#include "delegate/DelegateOpt.h"
#include "port/transport/common/ITransport.h"
#include "port/transport/common/DmqHeader.h"
#include "port/transport/common/ITransportMonitor.h"


// lwIP Netconn Includes
#include "lwip/api.h"
#include "lwip/inet.h" 
#include "lwip/ip_addr.h"

#include <cstring>
#include <chrono>

namespace dmq::transport {

class NetconnUdpTransport : public ITransport
{
public:
    enum class Type
    {
        PUB,
        SUB
    };

    NetconnUdpTransport() : m_sendTransport(this), m_recvTransport(this)
    {
    }

    ~NetconnUdpTransport()
    {
        Close();
    }

    /// Initialize the Netconn UDP connection
    /// @param type PUB or SUB
    /// @param addr Target IP string (e.g. "192.168.1.50") for PUB; Ignored for SUB.
    /// @param port Local port to bind (SUB) or Remote port to target (PUB).
    /// @return 0 on success, -1 on failure
    int Create(Type type, const char* addr, uint16_t port)
    {
        const dmq::LockGuard<dmq::RecursiveMutex> recvLock(m_recvMutex);
        const dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        Close();    // a second Create() replaces the connection instead of leaking it
        m_type = type;
        m_remotePort = port;

        // 1. Create a new UDP connection
        m_conn = netconn_new(NETCONN_UDP);
        if (m_conn == nullptr)
        {
            return -1;
        }

        // 2. Configure Address
        if (type == Type::PUB)
        {
            // Parse string IP to LwIP ip_addr_t
            if (ipaddr_aton(addr, &m_remoteIp) == 0)
            {
                Close(); // Prevent memory leak on invalid IP
                return -1; 
            }
            netconn_set_recvtimeout(m_conn, static_cast<int>(dmq::UDP_PUB_RECV_TIMEOUT.count()));
        }
        else if (type == Type::SUB)
        {
            // Bind to all interfaces (IP_ADDR_ANY) on the specified port
            if (netconn_bind(m_conn, IP_ADDR_ANY, port) != ERR_OK)
            {
                Close(); // Prevent memory leak on bind failure
                return -1;
            }
            // Receive timeout, so the receive thread can check for exit
            netconn_set_recvtimeout(m_conn, static_cast<int>(dmq::UDP_SUB_RECV_TIMEOUT.count()));
        }

        return 0;
    }

    /// Clean up the netconn resources. Waits for a Receive() in progress on
    /// another thread to return (at most its receive timeout) first.
    void Close()
    {
        const dmq::LockGuard<dmq::RecursiveMutex> recvLock(m_recvMutex);
        const dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_conn)
        {
            netconn_delete(m_conn);
            m_conn = nullptr;
        }
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        const dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_conn)
        {
            netconn_set_recvtimeout(m_conn, static_cast<int>(timeout.count()));
        }
    }

    virtual int Send(xostringstream& os, const DmqHeader& header) override
    {
        const dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (os.bad() || os.fail() || !m_conn) {
            return -1;
        }

        // Only SUBs should block non-ACK traffic
        if (m_type == Type::SUB && header.GetId() != dmq::ACK_REMOTE_ID) {
            return -1;
        }

        if (m_sendTransport != this) {
            return -1;
        }

        // Get payload data without string copy if possible
        auto payload = os.str();
        if (payload.length() > (BUFFER_SIZE - DmqHeader::HEADER_SIZE)) {
            return -1;
        }
        uint16_t payloadLen = static_cast<uint16_t>(payload.length());

        // Prepare header values in Network Byte Order
        DmqHeader wireHeader = header;
        wireHeader.SetLength(payloadLen);
        wireHeader.Encode(m_sendBuffer);

        if (payloadLen > 0) {
            memcpy(m_sendBuffer + DmqHeader::HEADER_SIZE, payload.data(), payloadLen);
        }

        // Allocate a Netbuf
        struct netbuf* buf = netbuf_new();
        if (!buf) return -1;

        void* dataPtr = netbuf_alloc(buf, DmqHeader::HEADER_SIZE + payloadLen);
        if (!dataPtr) {
            netbuf_delete(buf);
            return -1;
        }

        // Copy to NetX/LwIP internal buffer
        memcpy(dataPtr, m_sendBuffer, DmqHeader::HEADER_SIZE + payloadLen);

        // PUB uses pre-configured IP; SUB uses last-received IP (Reply)
        err_t err = netconn_sendto(m_conn, buf, &m_remoteIp, m_remotePort);

        netbuf_delete(buf); 

        if (err != ERR_OK) return -1;

        return 0;
    }

    virtual int Receive(xstringstream& is, DmqHeader& header) override
    {
        // m_recvMutex, not m_mutex: a Send() on another thread (e.g. a PUB that
        // also drains its ACKs here) isn't held up for the receive timeout.
        const dmq::LockGuard<dmq::RecursiveMutex> recvLock(m_recvMutex);
        if (m_recvTransport != this || !m_conn) {
            return -1;
        }

        struct netbuf* buf = nullptr;
        err_t err = netconn_recv(m_conn, &buf);

        if (err == ERR_TIMEOUT) return RECEIVE_NO_DATA;
        if (err != ERR_OK || buf == nullptr) return -1;

        uint16_t len = netbuf_len(buf);
        if (len < DmqHeader::HEADER_SIZE) {
            netbuf_delete(buf);
            return -1;
        }

        // Read Header (Network Byte Order)
        char headerBuf[DmqHeader::HEADER_SIZE];
        netbuf_copy(buf, headerBuf, DmqHeader::HEADER_SIZE);

        header.Decode(headerBuf);

        // One datagram is one message: the header length must match exactly
        // and fit the buffer. Anything else is malformed and must not be
        // delivered (cut short) or ACKed.
        const uint16_t payloadSize = header.GetLength();
        if (!header.IsValid() || len != DmqHeader::HEADER_SIZE + payloadSize ||
            payloadSize > BUFFER_SIZE) {
            netbuf_delete(buf);
            return -1;
        }

        // A SUB replies to whoever sent the last valid message. Only a SUB, and
        // only now: a stray or malformed datagram must never redirect where a
        // PUB sends.
        if (m_type == Type::SUB) {
            const dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
            ip_addr_t* addr = netbuf_fromaddr(buf);
            if (addr) ip_addr_copy(m_remoteIp, *addr);
            m_remotePort = netbuf_fromport(buf);
        }

        // Extract Payload
        if (payloadSize > 0) {
            netbuf_copy_partial(buf, m_recvBuffer, payloadSize, DmqHeader::HEADER_SIZE);
            is.clear(); 
            is.str(""); 
            is.write(m_recvBuffer, payloadSize);
        }

        netbuf_delete(buf);

        // Handle Reliability Logic
        if (header.GetId() == dmq::ACK_REMOTE_ID) {
            if (m_transportMonitor) m_transportMonitor->Remove(header.GetSeqNum());
        }
        // ACK whenever a send path exists, whether or not this transport also
        // tracks its own sends: a receive-only transport (e.g. NetworkNode's SUB)
        // has no monitor but must still ACK the sender's RELIABLE messages.
        else if (m_sendTransport) {
            // Auto-ACK using Send logic
            dmq::xostringstream ss_ack;
            DmqHeader ack;
            ack.SetId(dmq::ACK_REMOTE_ID);
            ack.SetSeqNum(header.GetSeqNum());
            ack.SetLength(0);
            m_sendTransport->Send(ss_ack, ack); 
        }

        return 0;
    }

    void SetTransportMonitor(ITransportMonitor* monitor) { m_transportMonitor = monitor; }
    void SetSendTransport(ITransport* transport) { m_sendTransport = transport; }
    void SetRecvTransport(ITransport* transport) { m_recvTransport = transport; }

private:
    struct netconn* m_conn = nullptr;
    ip_addr_t m_remoteIp{};
    uint16_t  m_remotePort = 0;
    Type m_type = Type::PUB;

    ITransport* m_sendTransport = nullptr;
    ITransport* m_recvTransport = nullptr;
    ITransportMonitor* m_transportMonitor = nullptr;

    // Datagram buffer (header + payload): DMQ_UDP_BUFFER_SIZE, default the
    // 1500-byte Ethernet MTU.
    static constexpr size_t BUFFER_SIZE = dmq::UDP_BUFFER_SIZE;
    char m_recvBuffer[BUFFER_SIZE] = { 0 };
    char m_sendBuffer[BUFFER_SIZE] = { 0 };
    dmq::RecursiveMutex m_mutex;        ///< Connection open/close and Send() state (remote address)
    dmq::RecursiveMutex m_recvMutex;    ///< Held by Receive(); taken before m_mutex
};

}

#endif // ARM_LWIP_NETCONN_UDP_TRANSPORT_H