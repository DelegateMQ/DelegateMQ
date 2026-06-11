#ifndef WIN32_TCP_TRANSPORT_H
#define WIN32_TCP_TRANSPORT_H

/// @file Win32TcpTransport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief Win32 TCP transport implementation for DelegateMQ.
/// 
/// @details
/// This class implements the ITransport interface using Windows Sockets (Winsock2). 
/// It supports both CLIENT and SERVER modes for transmitting serialized delegate data.
/// In SERVER mode, it supports multiple simultaneous client connections and broadcasts
/// outgoing messages to all connected participants.
/// 
/// Key Features:
/// 1. **Direct I/O**: Executes socket operations directly on the calling thread, 
///    relying on OS-level thread safety to avoid deadlocks.
/// 2. **Reliability Support**: Integrates with `TransportMonitor` to track sequence 
///    numbers and acknowledge (ACK) receipts.
/// 3. **Non-Blocking I/O**: Utilizes `select()` in the receive loop to prevent 
///    thread blocking when no data is available, facilitating clean shutdowns.
/// 4. **Multi-Client Multiplexing**: SERVER mode manages a list of clients and 
///    polls them for data using `select()`.
/// 5. **Socket Management**: Use WinsockConnect class in main() for `WSAStartup` and 
///    socket creation/cleanup.

#if !defined(_WIN32) && !defined(_WIN64)
#error This code must be compiled as a Win32 or Win64 application.
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "port/transport/ITransport.h"
#include "port/transport/ITransportMonitor.h"
#include "port/transport/DmqHeader.h"
#include <windows.h>
#include <sstream>
#include <cstdio>
#include <iostream>
#include <vector>
#include <algorithm>
#include <mutex>

namespace dmq::transport {

/// @brief A TCP transport implementation for Win32 using Winsock.
class Win32TcpTransport : public ITransport
{
    XALLOCATOR
public:
    enum class Type
    {
        SERVER,
        CLIENT
    };

    Win32TcpTransport() : m_sendTransport(this), m_recvTransport(this)
    {
    }

    ~Win32TcpTransport()
    {
        Close();
    }

    /// @brief Create a TCP transport.
    /// @param type SERVER or CLIENT.
    /// @param addr The IP address string.
    /// @param port The TCP port.
    /// @return 0 on success, -1 on failure.
    int Create(Type type, LPCSTR addr, USHORT port)
    {
        m_type = type;
        sockaddr_in service;
        service.sin_family = AF_INET;
        service.sin_port = htons(port);
        inet_pton(AF_INET, addr, &service.sin_addr);

        if (type == Type::SERVER)
        {
            m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (m_listenSocket == INVALID_SOCKET) return -1;

            // Allow address reuse to facilitate quick restarts during development
            int opt = 1;
            setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

            if (bind(m_listenSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR)
            {
                std::cerr << "Win32TcpTransport: Bind failed: " << WSAGetLastError() << std::endl;
                Close();
                return -1;
            }

            if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
                std::cerr << "Win32TcpTransport: Listen failed." << std::endl;
                Close();
                return -1;
            }
            std::cout << "Win32TcpTransport: Server listening on " << port << "..." << std::endl;
        }
        else if (type == Type::CLIENT)
        {
            m_clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (connect(m_clientSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR)
            {
                // Silent fail for clients to support polling/retries in application layer
                closesocket(m_clientSocket);
                m_clientSocket = INVALID_SOCKET;
                return -1;
            }
            std::cout << "Win32TcpTransport: Client connected to " << addr << ":" << port << std::endl;

            DWORD timeout = 2000;
            setsockopt(m_clientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        }

        return 0;
    }

    /// @brief Close all sockets and clean up.
    void Close()
    {    
        // Close Client Socket
        if (m_clientSocket != INVALID_SOCKET) {
            shutdown(m_clientSocket, SD_BOTH); 
            closesocket(m_clientSocket);
            m_clientSocket = INVALID_SOCKET;
        }

        // Close all server-accepted clients
        {
            const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
            for (auto s : m_serverClients) {
                shutdown(s, SD_BOTH);
                closesocket(s);
            }
            m_serverClients.clear();
        }

        // Close Listen Socket
        if (m_listenSocket != INVALID_SOCKET) {
            closesocket(m_listenSocket);
            m_listenSocket = INVALID_SOCKET;
        }
    }

    /// @brief Set the receive timeout for all active sockets.
    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        m_recvTimeoutMs = static_cast<DWORD>(timeout.count());
        if (m_clientSocket != INVALID_SOCKET)
        {
            setsockopt(m_clientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&m_recvTimeoutMs, sizeof(m_recvTimeoutMs));
        }
        
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        for (auto s : m_serverClients) {
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&m_recvTimeoutMs, sizeof(m_recvTimeoutMs));
        }
    }

    /// @brief Send data over the TCP link.
    /// @details In SERVER mode, this broadcasts to all connected clients.
    virtual int Send(dmq::xostringstream& os, const DmqHeader& header) override
    {
        if (m_type == Type::CLIENT) {
            return SendToSocket(m_clientSocket, os, header);
        } else {
            const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
            int lastErr = 0;
            for (auto s : m_serverClients) {
                if (SendToSocket(s, os, header) != 0) lastErr = -1;
            }
            return lastErr;
        }
    }

    /// @brief Receive data from the TCP link.
    virtual int Receive(dmq::xstringstream& is, DmqHeader& header) override
    {
        if (m_type == Type::SERVER) {
            return ReceiveServer(is, header);
        } else {
            return ReceiveSocket(m_clientSocket, is, header);
        }
    }

    void SetTransportMonitor(ITransportMonitor* tm) { m_transportMonitor = tm; }
    void SetSendTransport(ITransport* st) { m_sendTransport = st; }
    void SetRecvTransport(ITransport* rt) { m_recvTransport = rt; }

private:
    /// @brief Internal helper to send to a specific socket.
    int SendToSocket(SOCKET s, dmq::xostringstream& os, const DmqHeader& header) {
        if (s == INVALID_SOCKET) return -1;

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
        const char* ptr = m_sendBuffer;
        int remaining = totalSize;

        while (remaining > 0)
        {
            int sent = send(s, ptr, remaining, 0);
            if (sent == SOCKET_ERROR) return -1;
            ptr += sent;
            remaining -= sent;
        }

        return 0;
    }

    /// @brief Internal helper to handle server-side multiplexing.
    int ReceiveServer(dmq::xstringstream& is, DmqHeader& header) {
        // 1. Lazy Accept: Check for new client connections
        TIMEVAL tv = { 0, 1000 }; // 1ms poll
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(m_listenSocket, &fds);

        if (select(0, &fds, NULL, NULL, &tv) > 0) {
            SOCKET client = accept(m_listenSocket, NULL, NULL);
            if (client != INVALID_SOCKET) {
                std::cout << "Win32TcpTransport: Accepted client connection." << std::endl;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&m_recvTimeoutMs, sizeof(m_recvTimeoutMs));
                const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
                m_serverClients.push_back(client);
            }
        }

        // 2. Poll all connected clients for data
        fd_set readfds;
        FD_ZERO(&readfds);
        {
            const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
            if (m_serverClients.empty()) return -1;
            for (auto s : m_serverClients) {
                FD_SET(s, &readfds);
            }
        }

        tv.tv_usec = 1000; // 1ms poll
        if (select(0, &readfds, NULL, NULL, &tv) > 0) {
            const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
            for (auto it = m_serverClients.begin(); it != m_serverClients.end(); ) {
                if (FD_ISSET(*it, &readfds)) {
                    int result = ReceiveSocket(*it, is, header);
                    if (result == -2) { // Disconnected
                        std::cout << "Win32TcpTransport: Client disconnected." << std::endl;
                        closesocket(*it);
                        it = m_serverClients.erase(it);
                        continue; 
                    }
                    return result;
                }
                ++it;
            }
        }
        return -1;
    }

    /// @brief Internal helper to read from a specific socket and parse DMQ protocol.
    int ReceiveSocket(SOCKET s, dmq::xstringstream& is, DmqHeader& header) {
        if (s == INVALID_SOCKET) return -1;

        // Poll check to prevent blocking if no data is waiting
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(s, &readfds);
        TIMEVAL tv = { 0, 1000 };
        if (select(0, &readfds, NULL, NULL, &tv) <= 0) return -1;

        // 1. Read Header
        char headerBuf[DmqHeader::HEADER_SIZE];
        if (!ReadExact(s, headerBuf, DmqHeader::HEADER_SIZE)) return -2; // Disconnected

        // Parse Header (Convert Network -> Host)
        uint16_t marker, id, seqNum, length;
        memcpy(&marker, headerBuf, 2);
        memcpy(&id,     headerBuf + 2, 2);
        memcpy(&seqNum, headerBuf + 4, 2);
        memcpy(&length, headerBuf + 6, 2);

        header.SetMarker(ntohs(marker));
        header.SetId(ntohs(id));
        header.SetSeqNum(ntohs(seqNum));
        header.SetLength(ntohs(length));

        if (header.GetMarker() != DmqHeader::MARKER) return -1;

        // 2. Read Payload
        uint16_t payloadSize = header.GetLength();
        if (payloadSize > 0)
        {
            if (payloadSize > BUFFER_SIZE) return -1;
            if (!ReadExact(s, m_recvBuffer, payloadSize)) return -2;
            is.write(m_recvBuffer, payloadSize);
        }

        // 3. Handle Acknowledgment
        if (header.GetId() == dmq::ACK_REMOTE_ID) {
            if (m_transportMonitor) m_transportMonitor->Remove(header.GetSeqNum());
        }
        else if (m_transportMonitor && m_sendTransport) {
            // Send ACK using a small stack buffer
            uint16_t a_marker = htons(DmqHeader::MARKER);
            uint16_t a_id     = htons(dmq::ACK_REMOTE_ID);
            uint16_t a_seqNum = htons(header.GetSeqNum());
            uint16_t a_length = 0;

            char ackBuf[DmqHeader::HEADER_SIZE];
            memcpy(ackBuf, &a_marker, 2);
            memcpy(ackBuf + 2, &a_id, 2);
            memcpy(ackBuf + 4, &a_seqNum, 2);
            memcpy(ackBuf + 6, &a_length, 2);

            send(s, ackBuf, DmqHeader::HEADER_SIZE, 0);
        }
        return 0;
    }

    /// @brief Read exactly 'size' bytes from the socket.
    bool ReadExact(SOCKET s, char* dest, int size)
    {
        int total = 0;
        while (total < size) {
            int r = recv(s, dest + total, size - total, 0);
            if (r <= 0) return false;
            total += r;
        }
        return true;
    }

    SOCKET m_listenSocket = INVALID_SOCKET;
    SOCKET m_clientSocket = INVALID_SOCKET;
    std::vector<SOCKET> m_serverClients;
    static const int BUFFER_SIZE = 4096;
    char m_recvBuffer[BUFFER_SIZE] = { 0 };
    char m_sendBuffer[BUFFER_SIZE] = { 0 };
    DWORD m_recvTimeoutMs = 2000;
    Type m_type = Type::SERVER;
    
    ITransport* m_sendTransport = nullptr;
    ITransport* m_recvTransport = nullptr;
    ITransportMonitor* m_transportMonitor = nullptr;
    dmq::RecursiveMutex m_mutex;
};

} // namespace dmq::transport

#endif
