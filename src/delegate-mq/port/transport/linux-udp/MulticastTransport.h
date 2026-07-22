#ifndef LINUX_MULTICAST_TRANSPORT_H
#define LINUX_MULTICAST_TRANSPORT_H

#include "delegate/DelegateOpt.h"
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <fcntl.h>
#include <errno.h>

namespace dmq::transport {

/// @brief Linux Multicast UDP transport implementation for DelegateMQ.
class MulticastTransport : public ITransport
{
public:
    enum class Type { PUB, SUB };

    MulticastTransport() = default;
    ~MulticastTransport() { Close(); }

    int Create(Type type, const char* groupAddr, uint16_t port, const char* localInterface = "0.0.0.0")
    {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        m_type = type;
        m_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_socket < 0) return -1;

        int reuse = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        // ALWAYS enable loopback for testing convenience
        int loop = 1;
        setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin_family = AF_INET;
        m_addr.sin_port = htons(port);

        if (type == Type::PUB) {
            m_addr.sin_addr.s_addr = inet_addr(groupAddr);
            
            struct in_addr interface_addr;
            interface_addr.s_addr = inet_addr(localInterface);
            setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr));

            int ttl = 3;
            setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        }
        else {
            m_addr.sin_addr.s_addr = INADDR_ANY;
            if (bind(m_socket, (struct sockaddr*)&m_addr, sizeof(m_addr)) < 0) { Close(); return -1; }

            struct ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = inet_addr(groupAddr);
            mreq.imr_interface.s_addr = inet_addr(localInterface);
            if (setsockopt(m_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
                Close(); return -1;
            }

            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        }
        return 0;
    }

    void Close() {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_socket != -1) {
            close(m_socket);
            m_socket = -1;
        }
    }

    virtual int Send(xostringstream& os, const DmqHeader& header) override {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_type != Type::PUB) return -1;
        auto payload = os.str();
        DmqHeader headerCopy = header;
        headerCopy.SetLength(static_cast<uint16_t>(payload.length()));

        xostringstream ss(std::ios::in | std::ios::out | std::ios::binary);
        uint16_t marker = htons(headerCopy.GetMarker());
        uint16_t id     = htons(headerCopy.GetId());
        uint16_t seqNum = htons(headerCopy.GetSeqNum());
        uint16_t length = htons(headerCopy.GetLength());

        ss.write((const char*)&marker, 2);
        ss.write((const char*)&id, 2);
        ss.write((const char*)&seqNum, 2);
        ss.write((const char*)&length, 2);
        ss.write(payload.data(), payload.size());

        auto data = ss.str();
        sendto(m_socket, data.c_str(), data.size(), 0, (struct sockaddr*)&m_addr, sizeof(m_addr));
        return 0;
    }

    virtual int Receive(xstringstream& is, DmqHeader& header) override {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_type != Type::SUB) return -1;
        ssize_t size = recvfrom(m_socket, m_buffer, sizeof(m_buffer), 0, NULL, NULL);

        if (size < (ssize_t)DmqHeader::HEADER_SIZE) return -1;

        xstringstream headerStream(std::ios::in | std::ios::out | std::ios::binary);
        headerStream.write(m_buffer, DmqHeader::HEADER_SIZE);
        headerStream.seekg(0);

        uint16_t val = 0;
        headerStream.read((char*)&val, 2); header.SetMarker(ntohs(val));
        headerStream.read((char*)&val, 2); header.SetId(ntohs(val));
        headerStream.read((char*)&val, 2); header.SetSeqNum(ntohs(val));
        headerStream.read((char*)&val, 2); header.SetLength(ntohs(val));

        if (header.GetMarker() != DmqHeader::MARKER) {
            // std::cerr << "[Multicast] Bad Marker: " << std::hex << header.GetMarker() << std::dec << std::endl;
            return -1;
        }

        int payloadSize = size - DmqHeader::HEADER_SIZE;
        if (header.GetLength() > payloadSize) return -1;
        is.write(m_buffer + DmqHeader::HEADER_SIZE, header.GetLength());
        return 0;
    }

private:
    int m_socket = -1;
    sockaddr_in m_addr{};
    Type m_type = Type::PUB;
    static const int BUFFER_SIZE = 4096;
    char m_buffer[BUFFER_SIZE] = { 0 };
    dmq::RecursiveMutex m_mutex;
};

} // namespace dmq::transport

#endif
