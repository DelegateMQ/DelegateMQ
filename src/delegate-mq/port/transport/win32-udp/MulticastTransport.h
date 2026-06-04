#ifndef WIN32_MULTICAST_TRANSPORT_H
#define WIN32_MULTICAST_TRANSPORT_H

#if !defined(_WIN32) && !defined(_WIN64)
#error This code must be compiled as a Win32 or Win64 application.
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include <windows.h>
#include <sstream>
#include <cstdio>
#include <iostream>

namespace dmq::transport {

class MulticastTransport : public ITransport
{
    XALLOCATOR
public:
    enum class Type { PUB, SUB };

    MulticastTransport() = default;
    ~MulticastTransport() { Close(); }

    int Create(Type type, LPCSTR groupAddr, USHORT port, LPCSTR localInterface = "0.0.0.0")
    {
        m_type = type;
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) return -1;

        int reuse = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        int loop = 1;
        setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));

        if (type == Type::PUB) {
            m_addr.sin_family = AF_INET;
            m_addr.sin_port = htons(port);
            inet_pton(AF_INET, groupAddr, &m_addr.sin_addr);

            // Disable loopback so we don't receive our own packets
            loop = 0;
            setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));

            in_addr localAddr;
            inet_pton(AF_INET, localInterface, &localAddr);
            setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&localAddr, sizeof(localAddr));
            
            std::cout << "[Multicast] Created PUB on " << localInterface << ", Group: " << groupAddr << ":" << port << std::endl;
        }
        else {
            m_addr.sin_family = AF_INET;
            m_addr.sin_port = htons(port);
            m_addr.sin_addr.s_addr = INADDR_ANY;

            if (::bind(m_socket, (sockaddr*)&m_addr, sizeof(m_addr)) == SOCKET_ERROR) return -1;

            ip_mreq mreq;
            inet_pton(AF_INET, groupAddr, &mreq.imr_multiaddr);
            inet_pton(AF_INET, localInterface, &mreq.imr_interface);
            if (setsockopt(m_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
                std::cerr << "[Multicast] Join Failed: " << WSAGetLastError() << std::endl;
                return -1;
            }
            
            DWORD timeout = 1000;
            setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
            std::cout << "[Multicast] Created SUB on " << localInterface << ", Group: " << groupAddr << ":" << port << std::endl;
        }
        return 0;
    }

    void Close() {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }

    virtual int Send(dmq::xostringstream& os, const DmqHeader& header) override {
        if (m_type != Type::PUB) return -1;
        auto payload = os.str();
        DmqHeader headerCopy = header;
        headerCopy.SetLength(static_cast<uint16_t>(payload.length()));

        dmq::xostringstream ss(std::ios::in | std::ios::out | std::ios::binary);
        uint16_t marker = htons(headerCopy.GetMarker());
        uint16_t id = htons(headerCopy.GetId());
        uint16_t seq = htons(headerCopy.GetSeqNum());
        uint16_t len = htons(headerCopy.GetLength());

        ss.write((char*)&marker, 2);
        ss.write((char*)&id, 2);
        ss.write((char*)&seq, 2);
        ss.write((char*)&len, 2);
        ss.write(payload.data(), payload.size());

        auto fullData = ss.str();
        int err = sendto(m_socket, fullData.data(), (int)fullData.length(), 0, (sockaddr*)&m_addr, sizeof(m_addr));
        
        if (err != SOCKET_ERROR) {
            // std::cout << "[Multicast] Sent " << fullData.length() << " bytes" << std::endl;
            return 0;
        }
        return -1;
    }

    virtual int Receive(dmq::xstringstream& is, DmqHeader& header) override {
        if (m_type != Type::SUB) return -1;
        int addrLen = sizeof(m_addr);
        int size = recvfrom(m_socket, m_buffer, sizeof(m_buffer), 0, (sockaddr*)&m_addr, &addrLen);
        
        if (size == SOCKET_ERROR) return -1;
        if (size <= (int)DmqHeader::HEADER_SIZE) return -1;

        dmq::xstringstream headerStream(std::ios::in | std::ios::out | std::ios::binary);
        headerStream.write(m_buffer, DmqHeader::HEADER_SIZE);
        headerStream.seekg(0);

        uint16_t val = 0;
        headerStream.read((char*)&val, 2); header.SetMarker(ntohs(val));
        headerStream.read((char*)&val, 2); header.SetId(ntohs(val));
        headerStream.read((char*)&val, 2); header.SetSeqNum(ntohs(val));
        headerStream.read((char*)&val, 2); header.SetLength(ntohs(val));

        is.write(m_buffer + DmqHeader::HEADER_SIZE, size - DmqHeader::HEADER_SIZE);
        return 0;
    }

private:
    SOCKET m_socket = INVALID_SOCKET;
    sockaddr_in m_addr{};
    Type m_type = Type::PUB;
    static const int BUFFER_SIZE = 4096;
    char m_buffer[BUFFER_SIZE] = { 0 };
};

} // namespace dmq::transport

#endif
