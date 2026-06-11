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
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include "port/transport/ITransportMonitor.h"
#include "nx_api.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

// Helper to check for standard endian headers
#if defined(__GNUC__) || defined(__clang__)
    #define NETX_HTONS(x) __builtin_bswap16(x)
    #define NETX_NTOHS(x) __builtin_bswap16(x)
#else
    inline uint16_t netx_swap16(uint16_t val) {
        return (val << 8) | (val >> 8);
    }
    #define NETX_HTONS(x) netx_swap16(x)
    #define NETX_NTOHS(x) netx_swap16(x)
#endif

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
        // Initialize mutex for socket protection (No Inherit to detect deadlocks easier)
        tx_mutex_create(&m_mutex, (CHAR*)"NetXTransportMutex", TX_NO_INHERIT);
        memset(&m_socket, 0, sizeof(m_socket));
    }

    ~NetXUdpTransport()
    {
        Close();
        tx_mutex_delete(&m_mutex);
    }

    /// @brief Initialize the UDP socket.
    int Create(Type type, const char* addr_str, uint16_t port)
    {
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
            tx_mutex_put(&m_mutex);
            return -1;
        }

        tx_mutex_put(&m_mutex);
        return 0;
    }

    void Close()
    {
        tx_mutex_get(&m_mutex, TX_WAIT_FOREVER);
        
        if (m_socket.nx_udp_socket_id != 0)
        {
            nx_udp_socket_unbind(&m_socket);
            nx_udp_socket_delete(&m_socket);
            memset(&m_socket, 0, sizeof(m_socket));
        }
        
        tx_mutex_put(&m_mutex);
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
        if (payload.length() > UINT16_MAX) {
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

        uint16_t marker = NETX_HTONS(headerCopy.GetMarker());
        uint16_t id     = NETX_HTONS(headerCopy.GetId());
        uint16_t seqNum = NETX_HTONS(headerCopy.GetSeqNum());
        uint16_t length = NETX_HTONS(headerCopy.GetLength());

        // 3. Append Data (With Error Checks)
        // Check return value of every append. If one fails, the packet is corrupted/partial.
        if (nx_packet_data_append(packet_ptr, &marker, sizeof(marker), m_pool, NX_NO_WAIT) != NX_SUCCESS) 
            goto cleanup_and_fail;

        if (nx_packet_data_append(packet_ptr, &id, sizeof(id), m_pool, NX_NO_WAIT) != NX_SUCCESS) 
            goto cleanup_and_fail;

        if (nx_packet_data_append(packet_ptr, &seqNum, sizeof(seqNum), m_pool, NX_NO_WAIT) != NX_SUCCESS) 
            goto cleanup_and_fail;

        if (nx_packet_data_append(packet_ptr, &length, sizeof(length), m_pool, NX_NO_WAIT) != NX_SUCCESS) 
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
        tx_mutex_get(&m_mutex, TX_WAIT_FOREVER);

        if (m_recvTransport != this) {
            tx_mutex_put(&m_mutex);
            return -1;
        }

        NX_PACKET* packet_ptr = nullptr;
        
        // Use configured timeout
        UINT ret = nx_udp_socket_receive(&m_socket, &packet_ptr, m_recvTimeout); 

        if (ret != NX_SUCCESS) {
            tx_mutex_put(&m_mutex);
            return -1; // No packet or error
        }

        // --- Extract Source Info for ACKs ---
        ULONG srcIp;
        UINT srcPort;
        nx_udp_source_extract(packet_ptr, &srcIp, &srcPort);

        if (m_type == Type::SUB) {
            m_targetIp = srcIp;
            m_port = srcPort;
        }

        // --- Read Data ---
        ULONG bytesRead;
        UCHAR buffer[1500]; 
        
        // Validate total length before attempting retrieve
        if (packet_ptr->nx_packet_length > sizeof(buffer)) {
            nx_packet_release(packet_ptr);
            tx_mutex_put(&m_mutex);
            return -1; // Packet too large for buffer
        }

        // Retrieve copies data out of packet (handling chained packets if necessary)
        ret = nx_packet_data_retrieve(packet_ptr, buffer, &bytesRead);
        
        nx_packet_release(packet_ptr);

        if (ret != NX_SUCCESS || bytesRead < DmqHeader::HEADER_SIZE) {
            tx_mutex_put(&m_mutex);
            return -1;
        }

        // Parse Header
        uint16_t* ptr16 = (uint16_t*)buffer;
        header.SetMarker(NETX_NTOHS(ptr16[0]));
        header.SetId(NETX_NTOHS(ptr16[1]));
        header.SetSeqNum(NETX_NTOHS(ptr16[2]));
        header.SetLength(NETX_NTOHS(ptr16[3]));

        if (header.GetMarker() != DmqHeader::MARKER) {
            tx_mutex_put(&m_mutex);
            return -1;
        }

        // Check payload bounds
        if (bytesRead < (ULONG)(DmqHeader::HEADER_SIZE + header.GetLength())) {
            tx_mutex_put(&m_mutex);
            return -1;
        }

        // Reset stream state before writing
        is.clear();
        is.str("");

        // Write Payload to stream
        is.write((char*)(buffer + DmqHeader::HEADER_SIZE), header.GetLength());

        // --- ACK Logic ---
        if (header.GetId() == dmq::ACK_REMOTE_ID) {
            if (m_transportMonitor) m_transportMonitor->Remove(header.GetSeqNum());
        }
        else if (m_transportMonitor && m_sendTransport) {
            // Send ACK
            xostringstream ss_ack;
            DmqHeader ack;
            ack.SetId(dmq::ACK_REMOTE_ID);
            ack.SetSeqNum(header.GetSeqNum());
            ack.SetLength(0);
            
            // Release lock before recursive call to Send to prevent deadlock
            tx_mutex_put(&m_mutex);
            m_sendTransport->Send(ss_ack, ack);
            return 0; 
        }

        tx_mutex_put(&m_mutex);
        return 0;
    }

    void SetTransportMonitor(ITransportMonitor* tm) { m_transportMonitor = tm; }
    void SetSendTransport(ITransport* st) { m_sendTransport = st; }
    void SetRecvTransport(ITransport* rt) { m_recvTransport = rt; }

private:
    NX_IP* m_ip = nullptr;
    NX_PACKET_POOL* m_pool = nullptr;
    NX_UDP_SOCKET m_socket;
    TX_MUTEX m_mutex;
    
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