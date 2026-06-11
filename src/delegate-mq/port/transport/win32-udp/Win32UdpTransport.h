#ifndef WIN32_UDP_TRANSPORT_H
#define WIN32_UDP_TRANSPORT_H

/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief Win32 UDP transport implementation for DelegateMQ.
/// 
/// @details
/// This class implements the ITransport interface using Windows Sockets (Winsock2) 
/// for connectionless UDP communication. It supports two modes: PUB (Publisher/Sender) 
/// and SUB (Subscriber/Receiver).
/// 
/// Key Features:
/// 1. **Message Oriented**: Transmits discrete packets containing serialized delegate 
///    arguments and framing headers.
/// 2. **Reliability Support**: Integrates with `TransportMonitor` to track outgoing 
///    sequence numbers and process incoming ACKs to detect packet loss.
/// 3. **Socket Management**: Use WinsockConnect class in main() for `WSAStartup` and 
///    socket creation/cleanup.
/// 
/// @note This implementation uses blocking sockets with timeouts (`SO_RCVTIMEO`) to 
/// prevent indefinite blocking during receive operations.

#if !defined(_WIN32) && !defined(_WIN64)
#error This code must be compiled as a Win32 or Win64 application.
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")  // Link with Winsock library

#include "port/transport/ITransport.h"
#include "port/transport/ITransportMonitor.h"
#include "port/transport/DmqHeader.h"
#include <windows.h>
#include <sstream>
#include <cstdio>
#include <mutex>

namespace dmq::transport {

/// @brief Win32 UDP transport example. 
class Win32UdpTransport : public ITransport
{
    XALLOCATOR
public:
    enum class Type
    {
        PUB,
        SUB
    };

    Win32UdpTransport() : m_sendTransport(this), m_recvTransport(this)
    {
    }

    ~Win32UdpTransport()
    {
        Close();
    }

    int Create(Type type, LPCSTR addr, USHORT port)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_type = type;

        // Create a UDP socket
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET)
        {
            std::cerr << "Socket creation failed." << std::endl;
            return -1;
        }

        if (type == Type::PUB)
        {
            // Set up the server address structure
            m_addr.sin_family = AF_INET;
            m_addr.sin_port = htons(port);  // Convert port to network byte order

            // Convert IP address string to binary form
            if (inet_pton(AF_INET, addr, &m_addr.sin_addr) != 1)
            {
                std::cerr << "Invalid IP address format: " << addr << std::endl;
                return -1;
            }

            DWORD timeout = 2;
            setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        }
        else if (type == Type::SUB)
        {
            // Set up the server address structure
            m_addr.sin_family = AF_INET;
            m_addr.sin_port = htons(port);  // Convert port to network byte order
            m_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to all available interfaces

            // Bind the socket to the address and port
            int err = ::bind(m_socket, (sockaddr*)&m_addr, sizeof(m_addr));
            if (err == SOCKET_ERROR)
            {
                int wsaErr = WSAGetLastError();
                std::cerr << "Win32UdpTransport: Bind failed on port " << port << ". Error: " << wsaErr << std::endl;
                return -1;
            }

            // Set a 2-second receive timeout
            DWORD timeout = 2000; // 2000 milliseconds = 2 seconds
            err = setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
            if (err == SOCKET_ERROR)
            {
                std::cerr << "setsockopt(SO_RCVTIMEO) failed: " << WSAGetLastError() << std::endl;
                return -1;
            }
        }

        return 0;
    }

    void Close()
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        // Protected check to avoid double-close
        // Note: In a distinct shutdown scenario, the race on m_socket is acceptable 
        // because the goal is simply to kill the handle.
        if (m_socket != INVALID_SOCKET)
        {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_socket != INVALID_SOCKET)
        {
            DWORD ms = static_cast<DWORD>(timeout.count());
            setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
        }
    }

    virtual int Send(dmq::xostringstream& os, const DmqHeader& header) override
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (os.bad() || os.fail()) {
            std::cout << "Error: xostringstream is in a bad state!" << std::endl;
            return -1;
        }

        if (m_type == Type::SUB && header.GetId() != dmq::ACK_REMOTE_ID) {
            std::cout << "Error: Cannot send (non-ACK) on SUB socket!" << std::endl;
            return -1;
        }

        if (m_sendTransport != this) {
            std::cout << "Error: This transport used for receive only!" << std::endl;
            return -1;
        }

        // Get payload data without string copy if possible
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

        int totalSize = DmqHeader::HEADER_SIZE + payloadLen;

        int err = sendto(m_socket, m_sendBuffer, totalSize, 0, (sockaddr*)&m_addr, sizeof(m_addr));

        if (err == SOCKET_ERROR) {
            std::cerr << "Win32UdpTransport: ERROR - sendto failed with " << WSAGetLastError() << std::endl;
        }

        return (err == SOCKET_ERROR) ? -1 : 0;
    }

    virtual int Receive(dmq::xstringstream& is, DmqHeader& header) override
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_recvTransport != this) {
            std::cout << "Error: This transport used for send only!" << std::endl;
            return -1;
        }

        sockaddr_in fromAddr{};
        int addrLen = sizeof(fromAddr);
        int size = recvfrom(m_socket, m_buffer, sizeof(m_buffer), 0, (sockaddr*)&fromAddr, &addrLen);
        if (m_type == Type::SUB)
            m_addr = fromAddr;
        if (size == SOCKET_ERROR || size < static_cast<int>(DmqHeader::HEADER_SIZE))
        {
            return -1;
        }

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

        if (header.GetMarker() != DmqHeader::MARKER) {
            return -1;
        }

        // 2. Write payload to provided stream
        uint16_t payloadSize = header.GetLength();
        if (payloadSize > (size - DmqHeader::HEADER_SIZE))
            payloadSize = static_cast<uint16_t>(size - DmqHeader::HEADER_SIZE);

        if (payloadSize > 0) {
            is.write(m_buffer + DmqHeader::HEADER_SIZE, payloadSize);
        }

        // 3. Handle Acknowledgment
        if (header.GetId() == dmq::ACK_REMOTE_ID)
        {
            if (m_transportMonitor)
                m_transportMonitor->Remove(header.GetSeqNum());
        }
        else if (m_sendTransport && m_type == Type::SUB)
        {
            // Send ACK using a small stack buffer to avoid any heap
            uint16_t a_marker = htons(DmqHeader::MARKER);
            uint16_t a_id     = htons(dmq::ACK_REMOTE_ID);
            uint16_t a_seqNum = htons(header.GetSeqNum());
            uint16_t a_length = 0;

            char ackBuf[DmqHeader::HEADER_SIZE] = {0};
            memcpy(ackBuf, &a_marker, 2);
            memcpy(ackBuf + 2, &a_id, 2);
            memcpy(ackBuf + 4, &a_seqNum, 2);
            memcpy(ackBuf + 6, &a_length, 2);

            sockaddr_in targetAddr = (m_type == Type::SUB) ? fromAddr : m_addr;
            sendto(m_socket, ackBuf, static_cast<int>(DmqHeader::HEADER_SIZE), 0, (sockaddr*)&targetAddr, sizeof(targetAddr));
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
    SOCKET m_socket = INVALID_SOCKET;
    sockaddr_in m_addr{};

    Type m_type = Type::PUB;

    ITransport* m_sendTransport = nullptr;
    ITransport* m_recvTransport = nullptr;
    ITransportMonitor* m_transportMonitor = nullptr;

    /// @note UDP datagrams larger than the network MTU (typically 1500 bytes) will be 
    /// fragmented by the IP layer. If any fragment is lost, the entire message is 
    /// discarded. For maximum reliability, keep serialized messages under 1400 bytes.
    /// Messages exceeding BUFFER_SIZE will be truncated and discarded by the OS.
    static const int BUFFER_SIZE = 4096;
    char m_buffer[BUFFER_SIZE] = { 0 };
    char m_sendBuffer[BUFFER_SIZE] = { 0 };
    dmq::RecursiveMutex m_mutex;
};

using UdpTransport = Win32UdpTransport;

} // namespace dmq::transport


#endif