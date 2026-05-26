#ifndef WIN32_PIPE_TRANSPORT_H
#define WIN32_PIPE_TRANSPORT_H

/// @file 
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// Transport callable argument data to/from a remote using Win32 data pipe. 

#if !defined(_WIN32) && !defined(_WIN64)
    #error This code must be compiled as a Win32 or Win64 application.
#endif

// Include Winsock for htons/ntohs consistency
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include <windows.h>
#include <sstream>
#include <cstdio>
#include <iostream>
#include <mutex>

namespace dmq::transport {

/// @brief Win32 data pipe transport example. 
class Win32PipeTransport : public ITransport
{
public:
    enum class Type 
    {
        PUB,
        SUB
    };

    Win32PipeTransport() = default;
    
    ~Win32PipeTransport()
    {
        Close();
    }

    int Create(Type type, LPCSTR pipeName)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_type = type;
        if (type == Type::PUB)
        {
            // Connect to an existing pipe (Client)
            m_hPipe = CreateFile(
                pipeName,       // pipe name 
                GENERIC_READ |  // read and write access 
                GENERIC_WRITE,
                0,              // no sharing 
                NULL,           // default security attributes
                OPEN_EXISTING,  // opens existing pipe 
                0,              // default attributes 
                NULL);          // no template file 
        }
        else if (type == Type::SUB)
        {
            // Create named pipe (Server)
            m_hPipe = CreateNamedPipe(
                pipeName,             // pipe name 
                PIPE_ACCESS_DUPLEX,       // read/write access 
                PIPE_TYPE_MESSAGE |       // message type pipe 
                PIPE_READMODE_MESSAGE |   // message-read mode 
                PIPE_NOWAIT,              // non-blocking mode 
                PIPE_UNLIMITED_INSTANCES, // max. instances  
                BUFFER_SIZE,              // output buffer size 
                BUFFER_SIZE,              // input buffer size 
                0,                        // client time-out 
                NULL);                    // default security attribute 
        }

        if (m_hPipe == INVALID_HANDLE_VALUE)
        {
            // DWORD dwError = GetLastError();
            // std::cout << "Create pipe failed: " << dwError << std::endl;
            return -1;
        }
        return 0;
    }

    void Close()
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_hPipe != INVALID_HANDLE_VALUE)
        {
            // DisconnectNamedPipe is a server (SUB) only API; skip for client (PUB)
            if (m_type == Type::SUB)
                DisconnectNamedPipe(m_hPipe);
            CloseHandle(m_hPipe);
            m_hPipe = INVALID_HANDLE_VALUE;
        }
    }

    void SetRecvTimeout(std::chrono::milliseconds timeout)
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        // Named pipes on Windows use different mechanisms for timeouts 
        // than sockets (e.g. SetCommTimeouts for serial or ReadFile with OVERLAPPED).
        // Since this transport currently uses PIPE_NOWAIT, we'll just store it.
        m_recvTimeout = timeout;
    }

    virtual int Send(dmq::xostringstream& os, const DmqHeader& header) override
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (os.bad() || os.fail())
            return -1;

        if (m_hPipe == INVALID_HANDLE_VALUE) return -1;

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

        DWORD totalSize = static_cast<DWORD>(DmqHeader::HEADER_SIZE + payloadLen);
        DWORD sentLen = 0;
        BOOL success = WriteFile(m_hPipe, m_sendBuffer, totalSize, &sentLen, NULL);

        if (!success || sentLen != totalSize)
            return -1;

        return 0;
    }

    virtual int Receive(dmq::xstringstream& is, DmqHeader& header) override
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_hPipe == INVALID_HANDLE_VALUE) return -1;

        // Check/Accept connection
        // Note: In non-blocking mode (PIPE_NOWAIT), this returns false immediately 
        // if connected, or error if already connected.
        BOOL connected = ConnectNamedPipe(m_hPipe, NULL) ?
            TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        
        // If not connected yet, we can't read.
        if (connected == FALSE && GetLastError() == ERROR_NO_DATA) {
             return -1; 
        }

        DWORD size = 0;
        BOOL success = ReadFile(m_hPipe, m_buffer, BUFFER_SIZE, &size, NULL);

        if (success == FALSE || size < DmqHeader::HEADER_SIZE)
            return -1;

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

        return 0;
    }

private:
    // Increase buffer to Max Packet Size (64KB) to avoid truncation
    static const int BUFFER_SIZE = 65536;
    char m_buffer[BUFFER_SIZE] = { 0 };
    char m_sendBuffer[BUFFER_SIZE] = { 0 };

    Type m_type = Type::PUB;
    HANDLE m_hPipe = INVALID_HANDLE_VALUE;
    std::chrono::milliseconds m_recvTimeout = std::chrono::milliseconds(2000);
    dmq::RecursiveMutex m_mutex;
};

} // namespace dmq::transport

#endif
