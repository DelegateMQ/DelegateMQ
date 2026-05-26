// SpyBridge.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Asynchronous DataBus monitoring bridge implementation.

#include "SpyBridge.h"
#include "port/serialize/serialize/msg_serialize.h"
#include "extras/util/ThreadMonitor.h"
#include <iostream>

void SpyBridge::Start(const std::string& address, uint16_t port, const std::string& nodeId) {
    Init(address, port, TransportType::UNICAST, nodeId);
    std::cout << "[SpyBridge] Starting Unicast bridge to " << address << ":" << port << " (Node: " << nodeId << ")" << std::endl;
}

void SpyBridge::StartMulticast(const std::string& groupAddr, uint16_t port, const std::string& nodeId, const std::string& localInterface) {
    Init(groupAddr, port, TransportType::MULTICAST, nodeId, localInterface);
    std::cout << "[SpyBridge] Starting Multicast bridge to " << groupAddr << ":" << port << " using interface " << localInterface << " (Node: " << nodeId << ")" << std::endl;
}

void SpyBridge::Init(const std::string& address, uint16_t port, TransportType type, const std::string& nodeId, const std::string& localInterface) {
    auto& instance = GetInstance();
    if (instance.thread) return;

    instance.address = address;
    instance.port = port;
    instance.localInterface = localInterface;
    instance.type = type;
    instance.nodeId = nodeId;

    // Create a dedicated bridge thread with DROP policy to prevent stalling publishers
    instance.thread = std::make_unique<dmq::os::Thread>("SpyBridge", 200, dmq::os::FullPolicy::DROP);
    dmq::util::ThreadMonitor::Register(instance.thread.get());
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

        // Add nodeId to packet before sending
        dmq::databus::SpyPacket outgoing = packet;
        outgoing.nodeId = inst.nodeId;

        static serialize ms;
        static std::ostringstream oss(std::ios::binary);
        oss.str("");
        oss.clear();
        ms.write(oss, outgoing);

        if (oss.good()) {
            const std::string& s = oss.str();
            inst.telemetrySocket.Send(s.data(), s.size());
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
