// SpyBridge.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Asynchronous DataBus monitoring bridge implementation.

#include "SpyBridge.h"
#include "port/serialize/serialize/msg_serialize.h"
#include <iostream>
#include <sstream>

void SpyBridge::Start(const std::string& address, uint16_t port) {
    Init(address, port, TransportType::UNICAST);
    std::cout << "[SpyBridge] Starting Unicast bridge to " << address << ":" << port << std::endl;
}

void SpyBridge::StartMulticast(const std::string& groupAddr, uint16_t port, const std::string& localInterface) {
    Init(groupAddr, port, TransportType::MULTICAST, localInterface);
    std::cout << "[SpyBridge] Starting Multicast bridge to " << groupAddr << ":" << port << " using interface " << localInterface << std::endl;
}

void SpyBridge::Init(const std::string& address, uint16_t port, TransportType type, const std::string& localInterface) {
    auto& instance = GetInstance();
    if (instance.thread) return;

    instance.address = address;
    instance.port = port;
    instance.localInterface = localInterface;
    instance.type = type;

    // Create a dedicated bridge thread with DROP policy to prevent stalling publishers
    instance.thread = std::make_unique<dmq::os::Thread>("SpyBridge", 1000, dmq::os::FullPolicy::DROP);
    instance.thread->CreateThread();

    // Initialize the socket
    instance.telemetrySocket.Create();
    if (type == TransportType::MULTICAST) {
#ifdef _WIN32
        in_addr localAddr;
        inet_pton(AF_INET, localInterface.c_str(), &localAddr);
        setsockopt(instance.telemetrySocket.GetSocket(), IPPROTO_IP, IP_MULTICAST_IF, (const char*)&localAddr, sizeof(localAddr));
        int loop = 0; 
        setsockopt(instance.telemetrySocket.GetSocket(), IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));
        int ttl = 3;
        setsockopt(instance.telemetrySocket.GetSocket(), IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
#else
        int loop = 0; 
        setsockopt(instance.telemetrySocket.GetSocket(), IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
        int ttl = 3;
        setsockopt(instance.telemetrySocket.GetSocket(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
#endif
    }
    instance.telemetrySocket.Connect(address, port);

    // Subscribe to DataBus::Monitor asynchronously. The lambda will be invoked on the SpyBridge thread.
    instance.monitorConn = dmq::databus::DataBus::Monitor([](const dmq::databus::SpyPacket& packet) {
        auto& inst = GetInstance();
        static serialize ms; 
        std::ostringstream oss(std::ios::binary);
        ms.write(oss, packet);
        if (oss.good()) {
            std::string buffer = oss.str();
            inst.telemetrySocket.Send(buffer.data(), buffer.size());
        }
    }, instance.thread.get());
}

void SpyBridge::Stop() {
    auto& instance = GetInstance();
    if (!instance.thread) return;

    instance.monitorConn.Disconnect();
    instance.thread->ExitThread();
    instance.thread.reset();
    instance.telemetrySocket.Close();
}
