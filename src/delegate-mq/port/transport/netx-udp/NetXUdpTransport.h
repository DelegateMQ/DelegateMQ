#ifndef NETX_UDP_TRANSPORT_H
#define NETX_UDP_TRANSPORT_H

/// @file NetXUdpTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief NetX (ThreadX) UDP transport implementation for DelegateMQ.
///
/// @details
/// This class implements the ITransport interface using the Azure RTOS NetX (or NetX Duo)
/// network stack. It is designed for embedded targets running ThreadX.
///
/// **Dependencies:**
/// * ThreadX (`tx_api.h`)
/// * NetX (`nx_api.h`)
/// * A valid `NX_IP` instance and `NX_PACKET_POOL` must be provided at construction.
///
/// **Key Features:**
/// 1.  **Direct Execution**: Executes network operations directly on the calling thread.
/// 2.  **Thread Safety**: Uses a `TX_MUTEX` to protect the NetX socket.
/// 3.  **Reliability**: Integrated with `TransportMonitor` for ACKs/Retries.
/// 4.  **Robustness**: Validates packet allocation and append results to prevent corruption.

#include "delegate/DelegateOpt.h"
#include "port/transport/common/ITransport.h"
#include "port/transport/common/DmqHeader.h"
#include "port/transport/common/ITransportMonitor.h"
#include "nx_api.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace dmq::transport {

class NetXUdpTransport : public ITransport
{
    XALLOCATOR
public:
    enum class Type
    {
        PUB,
        SUB
    };

    /// @brief Constructor
    /// @param ip_ptr Pointer to the initialized NetX IP instance.
    /// @param pool_ptr Pointer to the packet pool for allocating UDP packets.
    NetXUdpTransport(NX_IP* ip_ptr, NX_PACKET_POOL* pool_ptr)
        : m_ip(ip_ptr)
        , m_pool(pool_ptr)
        , m_sendTransport(this)
        , m_recvTransport(this)
    {
        // Initialize mutexes for socket protection (No Inherit to detect deadlocks easier).
        // m_recvMutex is held by Receive(); m_mutex by Send() and socket setup.
        // Lock order: m_recvMutex, then m_mutex.
        tx_mutex_create(&m_mutex, (CHAR*)"NetXTransportMutex", TX_NO_INHERIT);
        tx_mutex_create(&m_recvMutex, (CHAR*)"NetXTransportRecvMutex", TX_NO_INHERIT);
        memset(&m_socket, 0, sizeof(m_socket));
    }

    ~NetXUdpTransport()
    {
        Close();
        tx_mutex_delete(&m_recvMutex);
        tx_mutex_delete(&m_mutex);
    }

    /// @brief Initialize the UDP socket.
    int Create(Type type, const char* addr_str, uint16_t port)
    {
        Close();    // a second Create() replaces the socket instead of leaking it
        tx_mutex_get(&m_recvMutex, TX_WAIT_FOREVER);
        tx_mutex_get(&m_mutex, TX_WAIT_FOREVER);

        m_type = type;
        m_port = port;

        // Parse IP Address
        m_targetIp = 0;
        if (addr_str) {
            uint32_t a, b, c, d;
            if (std::sscanf(addr_str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
                // Construct IP ULONG (Assumes NetX expects Host Byte Order for setup)
                m_targetIp = (a << 24) | (b << 16) | (c << 8) | d;
            }
        }

        // Create the UDP Socket
        UINT ret = nx_udp_socket_create(m_ip, &m_socket, (CHAR*)"DelegateMQ_Sock", 
                                        NX_IP_NORMAL, NX_FRAGMENT_OKAY, 0x80, 5);
        if (ret != NX_SUCCESS) {
            tx_mutex_put(&m_mutex);
            tx_mutex_put(&m_recvMutex);
            return -1;
        }

        // Bind
        if (type == Type::SUB) {
            ret = nx_udp_socket_bind(&m_socket, port, NX_WAIT_FOREVER);
        } else {
            ret = nx_udp_socket_bind(&m_socket, NX_ANY_PORT, NX_WAIT_FOREVER);
        }

        if (ret != NX_SUCCESS) {
            nx_udp_socket_delete(&m_socket);
            memset(&m_socket, 0, sizeof(m_socket));
            tx_mutex_put(&m_mutex);
            tx_mutex_put(&m_recvMutex);
            return -1;
        }

        SetRecvTimeout(type == Type::PUB ? dmq::UDP_PUB_RECV_TIMEOUT : dmq::UDP_SUB_RECV_TIMEOUT);

        tx_mutex_put(&m_mutex);
        tx_mutex_put(&m_recvMutex);
        return 0;
    }

    /// @details Waits for a Receive() in progress on another thread to return
    /// (at most its receive timeout) first.
    void Close()
    {
        tx_mutex_get(&m_recvMutex, TX_WAIT_FOREVER);
        tx_mutex_get(&m_mutex, TX_WAIT_FOREVER);

        if (m_socket.nx_udp_socket_id != 0)
        {
            nx_udp_socket_unbind(&m_socket);
            nx_udp_socket_delete(&m_socket);
            memset(&m_socket, 0, sizeof(m_socket));
        }

        tx_mutex_put(&m_mutex);
        tx_mutex_put(&m_recvMutex);
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        tx_mutex_get(&m_mutex, TX_WAIT_FOREVER);
        // Convert ms to ThreadX ticks
        m_recvTimeout = (ULONG)((timeout.count() * TX_TIMER_TICKS_PER_SECOND) / 1000);
        if (m_recvTimeout == 0 && timeout.count() > 0) m_recvTimeout = 1;
        tx_mutex_put(&m_mutex);
    }

    virtual int Send(xostringstream& os, const DmqHeader& header) override
    {
        tx_mutex_get(&m_mutex, TX_WAIT_FOREVER);

        if (os.bad() || os.fail()) {
            tx_mutex_put(&m_mutex);
            return -1;
        }

        // Block regular data on SUB socket (only ACKs allowed)
        if (m_type == Type::SUB && header.GetId() != dmq::ACK_REMOTE_ID) {
            tx_mutex_put(&m_mutex);
            return -1;
        }
        
        if (m_sendTransport != this) {
            tx_mutex_put(&m_mutex);
            return -1;
        }

        auto payload = os.str();
        // The receiver's buffer (same DMQ_UDP_BUFFER_SIZE) must hold the datagram.
        if (payload.length() > dmq::UDP_BUFFER_SIZE - DmqHeader::HEADER_SIZE) {
            tx_mutex_put(&m_mutex);
            return -1;
        }

        // 1. Allocate NetX Packet
        NX_PACKET* packet_ptr = nullptr;
        UINT ret = nx_packet_allocate(m_pool, &packet_ptr, NX_UDP_PACKET, NX_NO_WAIT);
        if (ret != NX_SUCCESS) {
            tx_mutex_put(&m_mutex);
            return -1;
        }

        // 2. Prepare Header (Network Byte Order)
        DmqHeader headerCopy = header;
        headerCopy.SetLength(static_cast<uint16_t>(payload.length()));

        UCHAR headerBuf[DmqHeader::HEADER_SIZE];
        headerCopy.Encode(headerBuf);

        // 3. Append Data (With Error Checks)
        // Check return value of every append. If one fails, the packet is corrupted/partial.
        if (nx_packet_data_append(packet_ptr, headerBuf, sizeof(headerBuf), m_pool, NX_NO_WAIT) != NX_SUCCESS) 
            goto cleanup_and_fail;

        if (nx_packet_data_append(packet_ptr, (VOID*)payload.data(), payload.size(), m_pool, NX_NO_WAIT) != NX_SUCCESS) 
            goto cleanup_and_fail;

        // 4. Send
        // nx_udp_socket_send typically releases the packet on internal errors.
        // However, if it fails due to binding issues (before driver), it might not.
        ret = nx_udp_socket_send(&m_socket, packet_ptr, m_targetIp, m_port);

        if (ret != NX_SUCCESS) {
            // Defensive release: Verify if your specific NetX version auto-releases on specific errors.
            // Standard practice is often to release if send fails.
            nx_packet_release(packet_ptr);
            tx_mutex_put(&m_mutex);
            return -1;
        }

        tx_mutex_put(&m_mutex);
        return 0;

    cleanup_and_fail:
        nx_packet_release(packet_ptr);
        tx_mutex_put(&m_mutex);
        return -1;
    }

    virtual int Receive(xstringstream& is, DmqHeader& header) override
    {
        // m_recvMutex, not m_mutex: a Send() on another thread isn't held up
        // for the receive timeout.
        tx_mutex_get(&m_recvMutex, TX_WAIT_FOREVER);
        const int result = ReceiveLocked(is, header);
        tx_mutex_put(&m_recvMutex);
        return result;
    }

    void SetTransportMonitor(ITransportMonitor* tm) { m_transportMonitor = tm; }
    void SetSendTransport(ITransport* st) { m_sendTransport = st; }
    void SetRecvTransport(ITransport* rt) { m_recvTransport = rt; }

private:
    /// Receive() body; caller holds m_recvMutex.
    int ReceiveLocked(xstringstream& is, DmqHeader& header)
    {
        if (m_recvTransport != this || m_socket.nx_udp_socket_id == 0) {
            return -1;
        }

        NX_PACKET* packet_ptr = nullptr;

        // Use configured timeout
        UINT ret = nx_udp_socket_receive(&m_socket, &packet_ptr, m_recvTimeout);
        if (ret == NX_NO_PACKET) {
            return RECEIVE_NO_DATA;
        }
        if (ret != NX_SUCCESS) {
            return -1;
        }

        // --- Extract Source Info for ACKs ---
        ULONG srcIp = 0;
        UINT srcPort = 0;
        nx_udp_source_extract(packet_ptr, &srcIp, &srcPort);

        // Validate total length before attempting retrieve
        if (packet_ptr->nx_packet_length > sizeof(m_recvBuffer)) {
            nx_packet_release(packet_ptr);
            return -1; // Packet too large for buffer
        }

        // Retrieve copies data out of packet (handling chained packets if necessary)
        ULONG bytesRead = 0;
        ret = nx_packet_data_retrieve(packet_ptr, m_recvBuffer, &bytesRead);
        nx_packet_release(packet_ptr);

        if (ret != NX_SUCCESS || bytesRead < DmqHeader::HEADER_SIZE) {
            return -1;
        }

        // Parse Header
        header.Decode(m_recvBuffer);
        if (!header.IsValid()) {
            return -1;
        }

        // One datagram is one message: the header length must match exactly.
        // Anything else is malformed and must not be delivered or ACKed.
        if (bytesRead != (ULONG)(DmqHeader::HEADER_SIZE + header.GetLength())) {
            return -1;
        }

        // A SUB replies to whoever sent the last valid message. Updated only
        // now, after validation, and under m_mutex since Send() uses it.
        if (m_type == Type::SUB) {
            tx_mutex_get(&m_mutex, TX_WAIT_FOREVER);
            m_targetIp = srcIp;
            m_port = srcPort;
            tx_mutex_put(&m_mutex);
        }

        // Reset stream state before writing
        is.clear();
        is.str("");

        // Write Payload to stream
        is.write((char*)(m_recvBuffer + DmqHeader::HEADER_SIZE), header.GetLength());

        // --- ACK Logic ---
        if (header.GetId() == dmq::ACK_REMOTE_ID) {
            if (m_transportMonitor) m_transportMonitor->Remove(header.GetSeqNum());
        }
        // ACK whenever a send path exists, whether or not this transport also
        // tracks its own sends: a receive-only transport (e.g. NetworkNode's SUB)
        // has no monitor but must still ACK the sender's RELIABLE messages.
        else if (m_sendTransport) {
            xostringstream ss_ack;
            DmqHeader ack;
            ack.SetId(dmq::ACK_REMOTE_ID);
            ack.SetSeqNum(header.GetSeqNum());
            ack.SetLength(0);
            m_sendTransport->Send(ss_ack, ack);   // takes m_mutex; m_recvMutex is held: same lock order
        }

        return 0;
    }

    NX_IP* m_ip = nullptr;
    NX_PACKET_POOL* m_pool = nullptr;
    NX_UDP_SOCKET m_socket;
    TX_MUTEX m_mutex;           ///< Socket setup and Send() state (target address)
    TX_MUTEX m_recvMutex;       ///< Held by Receive(); taken before m_mutex

    /// Receive buffer (header + payload), DMQ_UDP_BUFFER_SIZE. A member, not a
    /// stack array: thread stacks on these targets are small.
    UCHAR m_recvBuffer[dmq::UDP_BUFFER_SIZE];
    
    ULONG m_targetIp = 0;
    UINT  m_port = 0;

    Type m_type = Type::PUB;

    ULONG m_recvTimeout = 1; // Default 1 tick

    ITransport* m_sendTransport = nullptr;
    ITransport* m_recvTransport = nullptr;
    ITransportMonitor* m_transportMonitor = nullptr;
};

}

#endif // NETX_UDP_TRANSPORT_H