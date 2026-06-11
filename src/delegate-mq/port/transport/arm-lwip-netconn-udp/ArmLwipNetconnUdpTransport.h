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
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include "port/transport/ITransportMonitor.h"

#include <vector>

// lwIP Netconn Includes
#include "lwip/api.h"
#include "lwip/inet.h" 
#include "lwip/ip_addr.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <mutex>

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
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
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
            netconn_set_recvtimeout(m_conn, 50);
        }
        else if (type == Type::SUB)
        {
            // Bind to all interfaces (IP_ADDR_ANY) on the specified port
            if (netconn_bind(m_conn, IP_ADDR_ANY, port) != ERR_OK)
            {
                Close(); // Prevent memory leak on bind failure
                return -1;
            }
            // Set a 200ms timeout to allow the thread to check for exit signals
            netconn_set_recvtimeout(m_conn, 200);
        }

        return 0;
    }

    /// Clean up the netconn resources
    void Close()
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_conn)
        {
            netconn_delete(m_conn);
            m_conn = nullptr;
        }
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_conn)
        {
            netconn_set_recvtimeout(m_conn, static_cast<int>(timeout.count()));
        }
    }

    virtual int Send(xostringstream& os, const DmqHeader& header) override
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
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
        uint16_t payloadLen = static_cast<uint16_t>(payload.length());
        
        if (payloadLen > (BUFFER_SIZE - DmqHeader::HEADER_SIZE)) {
            return -1;
        }

        // Prepare header values in Network Byte Order
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
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_recvTransport != this || !m_conn) {
            return -1;
        }

        struct netbuf* buf = nullptr;
        err_t err = netconn_recv(m_conn, &buf);

        if (err == ERR_TIMEOUT) return -1; 
        if (err != ERR_OK || buf == nullptr) return -1;

        // Capture Sender Info (for ACKs/Replies)
        ip_addr_t* addr = netbuf_fromaddr(buf);
        uint16_t port = netbuf_fromport(buf);
        if (addr) ip_addr_copy(m_remoteIp, *addr);
        m_remotePort = port;

        uint16_t len = netbuf_len(buf);
        if (len < DmqHeader::HEADER_SIZE) {
            netbuf_delete(buf);
            return -1;
        }

        // Read Header (Network Byte Order)
        char headerBuf[DmqHeader::HEADER_SIZE];
        netbuf_copy(buf, headerBuf, DmqHeader::HEADER_SIZE);

        uint16_t marker, id, seqNum, length;
        memcpy(&marker, headerBuf, 2);
        memcpy(&id,     headerBuf + 2, 2);
        memcpy(&seqNum, headerBuf + 4, 2);
        memcpy(&length, headerBuf + 6, 2);

        header.SetMarker(ntohs(marker));
        header.SetId(ntohs(id));
        header.SetSeqNum(ntohs(seqNum));
        header.SetLength(ntohs(length));

        if (header.GetMarker() != DmqHeader::MARKER || len < DmqHeader::HEADER_SIZE + header.GetLength()) {
            netbuf_delete(buf);
            return -1;
        }

        // Extract Payload
        uint16_t payloadSize = header.GetLength();
        if (payloadSize > 0) {
            if (payloadSize > BUFFER_SIZE) payloadSize = BUFFER_SIZE;
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
        else if (m_transportMonitor && m_sendTransport) {
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

    static const int BUFFER_SIZE = 1500;
    char m_recvBuffer[BUFFER_SIZE] = { 0 };
    char m_sendBuffer[BUFFER_SIZE] = { 0 };
    dmq::RecursiveMutex m_mutex;
};

}

#endif // ARM_LWIP_NETCONN_UDP_TRANSPORT_H