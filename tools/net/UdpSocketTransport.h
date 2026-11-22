// UdpSocketTransport.h
// @see https://github.com/DelegateMQ/DelegateMQ
// Send-only dmq::transport::ITransport over the tools' UdpSocket, for the
// desktop bridges (SpyBridge, NodeBridge).

#ifndef UDP_SOCKET_TRANSPORT_H
#define UDP_SOCKET_TRANSPORT_H

#include "DelegateMQ.h"
#include "UdpSocket.h"
#include <cstring>
#include <string>
#include <vector>

/// @brief Sends each message as one UDP datagram: the DelegateMQ message
/// header, then the payload, as every DelegateMQ transport does. The library's
/// desktop UDP transports don't set multicast options; this one can, which is
/// why the desktop bridges use it instead of them. An embedded target passes
/// its port's UDP transport to SpyReporter/NodeReporter directly.
class UdpSocketTransport : public dmq::transport::ITransport {
public:
    /// Open the socket and aim it at address:port. For a multicast group,
    /// localInterface picks the sending interface ("" or "0.0.0.0": the default).
    bool Open(const std::string& address, uint16_t port, bool multicast, const std::string& localInterface = "") {
        if (!m_socket.Create())
            return false;
        if (multicast) {
            if (!localInterface.empty() && localInterface != "0.0.0.0") {
                in_addr ifAddr{};
                inet_pton(AF_INET, localInterface.c_str(), &ifAddr);
                setsockopt(m_socket.GetSocket(), IPPROTO_IP, IP_MULTICAST_IF, (const char*)&ifAddr, sizeof(ifAddr));
            }
            int loop = 1;   // the consoles may run on this machine
            setsockopt(m_socket.GetSocket(), IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));
            int ttl = 3;
            setsockopt(m_socket.GetSocket(), IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
        }
        return m_socket.Connect(address, port);
    }

    void Close() { m_socket.Close(); }

    int Send(const dmq::ByteWriter& os, const dmq::transport::DmqHeader& header) override {
        using dmq::transport::DmqHeader;
        if (os.fail() || os.size() > dmq::MAX_MESSAGE_SIZE)
            return -1;
        const size_t total = DmqHeader::HEADER_SIZE + os.size();
        m_buffer.resize(total);
        DmqHeader wire = header;
        wire.SetLength(static_cast<uint16_t>(os.size()));
        wire.Encode(m_buffer.data());
        memcpy(m_buffer.data() + DmqHeader::HEADER_SIZE, os.data(), os.size());
        return m_socket.Send(m_buffer.data(), total) == static_cast<int>(total) ? 0 : -1;
    }

    int Receive(dmq::ByteReader& is, dmq::transport::DmqHeader&) override {
        is.assign(nullptr, 0);
        return -1;      // send only
    }

private:
    UdpSocket m_socket;
    std::vector<uint8_t> m_buffer;      // header + payload; the sending thread only
};

#endif // UDP_SOCKET_TRANSPORT_H
