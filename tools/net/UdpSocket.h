// UdpSocket.h
// @see https://github.com/DelegateMQ/DelegateMQ
// Minimal cross-platform UDP socket wrapper.

#ifndef UDP_SOCKET_H
#define UDP_SOCKET_H

#include <string>
#include <cstdint>
#include <vector>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>
#endif

class UdpSocket {
public:
    UdpSocket() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    ~UdpSocket() {
        Close();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool Create() {
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
        return m_socket != INVALID_SOCKET;
#else
        return m_socket >= 0;
#endif
    }

    void Close() {
#ifdef _WIN32
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
#else
        if (m_socket >= 0) {
            close(m_socket);
            m_socket = -1;
        }
#endif
    }

    /// @param shared  true lets other sockets bind the same port. Needed for
    ///                multicast, where several listeners on one group are normal.
    ///                Leave false for a unicast listener: each unicast datagram
    ///                reaches only one socket, so a second listener would silently
    ///                starve one of them. Exclusive, the second bind fails instead.
    bool Bind(uint16_t port, const std::string& localInterface = "0.0.0.0", bool shared = false) {
        int opt = 1;
        if (shared) {
            setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#ifndef _WIN32
#ifdef SO_REUSEPORT
            setsockopt(m_socket, SOL_SOCKET, SO_REUSEPORT, (const char*)&opt, sizeof(opt));
#endif
#endif
        } else {
#ifdef _WIN32
            // Without this, a later socket that sets SO_REUSEADDR can still take
            // over the port on Windows.
            setsockopt(m_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&opt, sizeof(opt));
#endif
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, localInterface.c_str(), &addr.sin_addr) != 1) {
            addr.sin_addr.s_addr = INADDR_ANY;
        }

        if (bind(m_socket, (sockaddr*)&addr, sizeof(addr)) < 0) {
            return false;
        }
        return true;
    }

    bool JoinGroup(const std::string& groupAddr, const std::string& localInterface = "0.0.0.0") {
        int loop = 1;
        setsockopt(m_socket, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));

        ip_mreq mreq;
        if (inet_pton(AF_INET, groupAddr.c_str(), &mreq.imr_multiaddr) != 1) return false;
        if (inet_pton(AF_INET, localInterface.c_str(), &mreq.imr_interface) != 1) {
            mreq.imr_interface.s_addr = INADDR_ANY;
        }

        if (setsockopt(m_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq)) < 0) {
            return false;
        }
        return true;
    }

    /// Create, bind and (with a group) join multicast, for a tool's receive socket.
    /// The port is shared only in multicast mode; see Bind().
    /// @return Empty on success, otherwise an error message for the user.
    std::string Listen(uint16_t port, const std::string& multicastGroup, const std::string& localInterface) {
        if (!Create())
            return "ERROR: Failed to create socket";
        bool multicast = !multicastGroup.empty();
        if (!Bind(port, "0.0.0.0", multicast)) {
            std::string err = "ERROR: Could not bind to UDP port " + std::to_string(port);
            if (!multicast)
                err += ". Is another tool already listening on it?";
            return err;
        }
        if (multicast && !JoinGroup(multicastGroup, localInterface.empty() ? "0.0.0.0" : localInterface))
            return "ERROR: Could not join group " + multicastGroup;
        return "";
    }

    bool Connect(const std::string& address, uint16_t port) {
        m_remoteAddr.sin_family = AF_INET;
        m_remoteAddr.sin_port = htons(port);
        if (inet_pton(AF_INET, address.c_str(), &m_remoteAddr.sin_addr) != 1) {
            return false;
        }
        return true;
    }

    int Send(const void* data, size_t size) {
        return sendto(m_socket, (const char*)data, (int)size, 0, (sockaddr*)&m_remoteAddr, sizeof(m_remoteAddr));
    }

    int Receive(void* data, size_t size) {
        sockaddr_in fromAddr{};
#ifdef _WIN32
        int addrLen = sizeof(fromAddr);
#else
        socklen_t addrLen = sizeof(fromAddr);
#endif
        int result = recvfrom(m_socket, (char*)data, (int)size, 0, (sockaddr*)&fromAddr, &addrLen);
        if (result > 0) {
            m_remoteAddr = fromAddr;
        }
        return result;
    }

    void SetReceiveTimeout(uint32_t ms) {
#ifdef _WIN32
        DWORD timeout = ms;
        setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif
    }

    std::string GetRemoteAddress() const {
        char buf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &m_remoteAddr.sin_addr, buf, sizeof(buf))) {
            return std::string(buf);
        }
        return "0.0.0.0";
    }

#ifdef _WIN32
    SOCKET GetSocket() const { return m_socket; }
#else
    int GetSocket() const { return m_socket; }
#endif

private:
#ifdef _WIN32
    SOCKET m_socket = INVALID_SOCKET;
#else
    int m_socket = -1;
#endif
    sockaddr_in m_remoteAddr{};
};

#endif // UDP_SOCKET_H
