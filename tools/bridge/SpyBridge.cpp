// SpyBridge.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Asynchronous DataBus monitoring bridge implementation.

#include "SpyBridge.h"
#include "../src/UdpSocket.h"
#include "port/serialize/serialize/msg_serialize.h"
#include <iostream>
#include <sstream>

#if defined(_WIN32) || defined(_WIN64)
#include "port/transport/win32-udp/MulticastTransport.h"
#else
#include "port/transport/linux-udp/MulticastTransport.h"
#endif

void SpyBridge::Start(const std::string& address, uint16_t port) {
    auto& instance = GetInstance();
    if (instance.running) return;

    Init(address, port, TransportType::UNICAST);

    std::cout << "[SpyBridge] Starting Unicast bridge to " << address << ":" << port << std::endl;
}

void SpyBridge::StartMulticast(const std::string& groupAddr, uint16_t port, const std::string& localInterface) {
    auto& instance = GetInstance();
    if (instance.running) return;

    Init(groupAddr, port, TransportType::MULTICAST, localInterface);

    std::cout << "[SpyBridge] Starting Multicast bridge to " << groupAddr << ":" << port << " using interface " << localInterface << std::endl;
}

void SpyBridge::Init(const std::string& address, uint16_t port, TransportType type, const std::string& localInterface) {
    auto& instance = GetInstance();
    instance.address = address;
    instance.port = port;
    instance.localInterface = localInterface;
    instance.type = type;
    instance.running = true;

    instance.thread = std::thread(Worker);

    instance.monitorConn = dmq::databus::DataBus::Monitor([](const dmq::databus::SpyPacket& packet) {
        auto& inst = GetInstance();
        std::lock_guard<std::mutex> lock(inst.mutex);
        inst.queue.push(packet);
        if (inst.queue.size() > 1000) inst.queue.pop();
        inst.cv.notify_one();
    });
}

void SpyBridge::Stop() {
    auto& instance = GetInstance();
    if (!instance.running) return;

    instance.monitorConn.Disconnect();
    instance.running = false;
    instance.cv.notify_all();

    if (instance.thread.joinable()) {
        instance.thread.join();
    }
}

void SpyBridge::Worker() {
    auto& instance = GetInstance();

    UdpSocket socket;
    if (!socket.Create()) {
        std::cerr << "[SpyBridge] Failed to create socket" << std::endl;
        return;
    }

    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(instance.port);
    inet_pton(AF_INET, instance.address.c_str(), &destAddr.sin_addr);

    if (instance.type == TransportType::MULTICAST) {
#ifdef _WIN32
        in_addr localAddr;
        inet_pton(AF_INET, instance.localInterface.c_str(), &localAddr);
        setsockopt(socket.GetSocket(), IPPROTO_IP, IP_MULTICAST_IF, (const char*)&localAddr, sizeof(localAddr));
        int loop = 0; // Disable loopback to prevent the host from receiving its own transmissions
        setsockopt(socket.GetSocket(), IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));
        int ttl = 3;
        setsockopt(socket.GetSocket(), IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
#else
        int loop = 0; // Disable loopback to prevent the host from receiving its own transmissions
        setsockopt(socket.GetSocket(), IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
        int ttl = 3;
        setsockopt(socket.GetSocket(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
#endif
    } else {
        socket.Connect(instance.address, instance.port);
    }

    serialize ms;

    while (instance.running || !instance.queue.empty()) {
        dmq::databus::SpyPacket packet;
        {
            std::unique_lock<std::mutex> lock(instance.mutex);
            instance.cv.wait_for(lock, std::chrono::milliseconds(100), [&] {
                return !instance.queue.empty() || !instance.running;
            });

            if (instance.queue.empty()) {
                if (!instance.running) break;
                continue;
            }

            packet = std::move(instance.queue.front());
            instance.queue.pop();
        }

        std::ostringstream oss(std::ios::binary);
        ms.write(oss, packet);

        if (oss.good()) {
            std::string buffer = oss.str();
            int sent = sendto(socket.GetSocket(), buffer.data(), (int)buffer.size(), 0, (sockaddr*)&destAddr, sizeof(destAddr));
#ifdef _WIN32
            if (sent == SOCKET_ERROR) {
                // std::cerr << "[SpyBridge] sendto error: " << WSAGetLastError() << std::endl;
            }
#else
            if (sent < 0) {
                // std::cerr << "[SpyBridge] sendto error: " << errno << std::endl;
            }
#endif
        }
    }
}
