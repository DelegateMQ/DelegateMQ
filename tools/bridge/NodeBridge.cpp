// NodeBridge.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Opt-in node heartbeat bridge for the DelegateMQ Node Monitor.

#include "NodeBridge.h"
#include "../src/UdpSocket.h"
#include "port/serialize/serialize/msg_serialize.h"
#include "extras/util/NetworkConnect.h"
#include "extras/util/ThreadMonitorSer.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

static std::string GetHostname() {
    char buf[256] = {};
    gethostname(buf, sizeof(buf));
    return std::string(buf);
}

void NodeBridge::Start(const std::string& nodeId, const std::string& address, uint16_t port) {
    auto& instance = GetInstance();
    if (instance.running) return;

    instance.nodeId       = nodeId;
    instance.address      = address;
    instance.port         = port;
    instance.type         = TransportType::UNICAST;
    instance.hostname     = GetHostname();
    instance.ipAddress    = dmq::util::NetworkContext::GetLocalAddress();
    instance.startTime    = std::chrono::steady_clock::now();
    instance.totalMsgCount = 0;
    instance.topics.clear();
    instance.running      = true;

    std::cout << "[NodeBridge] Starting Unicast heartbeats to " << address << ":" << port
              << " as node \"" << nodeId << "\"" << std::endl;

    InitTelemetry(address, port, false);

    instance.thread = std::thread(Worker);
}

void NodeBridge::StartMulticast(const std::string& nodeId, const std::string& groupAddr,
                                uint16_t port, const std::string& localInterface) {
    auto& instance = GetInstance();
    if (instance.running) return;

    instance.nodeId        = nodeId;
    instance.address       = groupAddr;
    instance.localInterface = localInterface;
    instance.port          = port;
    instance.type          = TransportType::MULTICAST;
    instance.hostname      = GetHostname();
    instance.ipAddress     = (localInterface.empty() || localInterface == "0.0.0.0")
                                 ? dmq::util::NetworkContext::GetLocalAddress() : localInterface;
    instance.startTime     = std::chrono::steady_clock::now();
    instance.totalMsgCount = 0;
    instance.topics.clear();
    instance.running       = true;

    std::cout << "[NodeBridge] Starting Multicast heartbeats to " << groupAddr << ":" << port
              << " as node \"" << nodeId << "\" on interface " 
              << (localInterface.empty() ? "DEFAULT" : localInterface) << std::endl;

    InitTelemetry(groupAddr, port, true, localInterface);

    instance.thread = std::thread(Worker);
}

void NodeBridge::InitTelemetry(const std::string& address, uint16_t port, bool isMulticast, const std::string& localInterface) {
    auto& instance = GetInstance();

    // Subscribe to DataBus::Monitor to auto-discover topics and count messages.
    instance.monitorConn = dmq::databus::DataBus::Monitor([](const dmq::databus::SpyPacket& packet) {
        auto& inst = GetInstance();
        inst.totalMsgCount++;
        std::lock_guard<std::mutex> lock(inst.mutex);
        inst.topics.insert(packet.topic);
    });

    static dmq::util::ThreadStatsPacketSerializer serializer;
    dmq::databus::DataBus::RegisterSerializer<dmq::util::ThreadStatsPacket>("ThreadStats", serializer);
    dmq::databus::DataBus::RegisterStringifier<dmq::util::ThreadStatsPacket>("ThreadStats", dmq::util::ThreadStatsPacketToString);

    instance.telemetrySocket.Create();

    if (isMulticast && !localInterface.empty() && localInterface != "0.0.0.0") {
#ifdef _WIN32
        in_addr ifAddr;
        inet_pton(AF_INET, localInterface.c_str(), &ifAddr);
        setsockopt(instance.telemetrySocket.GetSocket(), IPPROTO_IP, IP_MULTICAST_IF, (const char*)&ifAddr, sizeof(ifAddr));
#else
        in_addr ifAddr;
        inet_pton(AF_INET, localInterface.c_str(), &ifAddr);
        setsockopt(instance.telemetrySocket.GetSocket(), IPPROTO_IP, IP_MULTICAST_IF, &ifAddr, sizeof(ifAddr));
#endif
    }

    instance.telemetrySocket.Connect(address, port);
    instance.isMulticast = isMulticast;

    instance.threadStatsConn = dmq::databus::DataBus::Subscribe<dmq::util::ThreadStatsPacket>("ThreadStats", [](const dmq::util::ThreadStatsPacket& packet) {
        auto& inst = GetInstance();
        std::ostringstream oss(std::ios::binary);
        serializer.Write(oss, packet);
        if (oss.good()) {
            std::string buf = oss.str();
            std::lock_guard<std::mutex> lock(inst.mutex);
            if (inst.running) {
                inst.telemetrySocket.Send(buf.data(), buf.size());
            }
        }
    });
}


void NodeBridge::Stop() {
    auto& instance = GetInstance();
    if (!instance.running) return;

    instance.monitorConn.Disconnect();
    instance.threadStatsConn.Disconnect();
    instance.running = false;

    if (instance.thread.joinable()) {
        instance.thread.join();
    }
}

void NodeBridge::Worker() {
    auto& instance = GetInstance();

    UdpSocket socket;
    if (!socket.Create()) {
        std::cerr << "[NodeBridge] Failed to create socket" << std::endl;
        return;
    }

    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port   = htons(instance.port);
    inet_pton(AF_INET, instance.address.c_str(), &destAddr.sin_addr);

    if (instance.type == TransportType::MULTICAST) {
#ifdef _WIN32
        in_addr localAddr{};
        inet_pton(AF_INET, instance.localInterface.c_str(), &localAddr);
        setsockopt(socket.GetSocket(), IPPROTO_IP, IP_MULTICAST_IF,
                   (const char*)&localAddr, sizeof(localAddr));
        int loop = 0; // Disable loopback to prevent the host from receiving its own transmissions
        setsockopt(socket.GetSocket(), IPPROTO_IP, IP_MULTICAST_LOOP,
                   (const char*)&loop, sizeof(loop));
        int ttl = 3;
        setsockopt(socket.GetSocket(), IPPROTO_IP, IP_MULTICAST_TTL,
                   (const char*)&ttl, sizeof(ttl));
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

    while (instance.running) {
        auto loopStart = std::chrono::steady_clock::now();

        // Build the heartbeat packet from current state
        dmq::NodeInfoPacket packet;
        packet.nodeId        = instance.nodeId;
        packet.hostname      = instance.hostname;
        packet.ipAddress     = instance.ipAddress;
        packet.totalMsgCount = instance.totalMsgCount.load();
        packet.uptimeMs      = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                loopStart - instance.startTime).count());

        {
            std::lock_guard<std::mutex> lock(instance.mutex);
            std::string joined;
            for (const auto& t : instance.topics) {
                if (!joined.empty()) joined += ';';
                joined += t;
            }
            packet.topicsStr = std::move(joined);
        }

        // Serialize and transmit
        std::ostringstream oss(std::ios::binary);
        ms.write(oss, packet);

        if (oss.good()) {
            std::string buffer = oss.str();
            sendto(socket.GetSocket(), buffer.data(), (int)buffer.size(),
                   0, (sockaddr*)&destAddr, sizeof(destAddr));
        }

        // Sleep for the remainder of the heartbeat interval
        auto elapsed   = std::chrono::steady_clock::now() - loopStart;
        auto remaining = std::chrono::milliseconds(HEARTBEAT_INTERVAL_MS) - elapsed;
        if (remaining > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(remaining);
        }
    }
}
