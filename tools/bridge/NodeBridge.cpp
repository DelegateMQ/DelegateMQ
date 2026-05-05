// NodeBridge.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Opt-in node heartbeat bridge implementation.

#include "NodeBridge.h"
#include "port/serialize/serialize/msg_serialize.h"
#include "extras/util/Timer.h"
#include "extras/util/ThreadMonitor.h"
#include "extras/util/ThreadMonitorSer.h"
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

void NodeBridge::Start(const std::string& nodeId, const std::string& address, uint16_t port) {
    auto& instance = GetInstance();
    instance.nodeId = nodeId;
    instance.type = TransportType::UNICAST;
    InitTelemetry(address, port, false);
}

void NodeBridge::StartMulticast(const std::string& nodeId, const std::string& groupAddr, uint16_t port, const std::string& localInterface) {
    auto& instance = GetInstance();
    instance.nodeId = nodeId;
    instance.type = TransportType::MULTICAST;
    InitTelemetry(groupAddr, port, true, localInterface);
}

void NodeBridge::InitTelemetry(const std::string& address, uint16_t port, bool isMulticast, const std::string& localInterface) {
    auto& instance = GetInstance();
    if (instance.thread) return;

    instance.address = address;
    instance.port = port;
    instance.localInterface = localInterface;
    instance.isMulticast = isMulticast;
    instance.startTime = std::chrono::steady_clock::now();

    // Get Hostname
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) instance.hostname = hostname;

    // Create a dedicated bridge thread
    instance.thread = std::make_unique<dmq::os::Thread>("NodeBridge", 100, dmq::os::FullPolicy::DROP);
    instance.thread->CreateThread();

    // Subscribe to DataBus::Monitor to auto-discover topics. Asynchronous delivery to NodeBridge thread.
    instance.monitorConn = dmq::databus::DataBus::Monitor([](const dmq::databus::SpyPacket& packet) {
        auto& inst = GetInstance();
        inst.totalMsgCount++;
        // Topics update is done on NodeBridge thread, so it's safe if we don't hit other threads.
        // We still need a lock if we access instance.topics from SendHeartbeat (also on NodeBridge thread).
        // Actually, if both are on NodeBridge thread, no lock is needed!
        inst.topics.insert(packet.topic);
    }, instance.thread.get());

    static dmq::util::ThreadStatsPacketSerializer serializer;
    dmq::databus::DataBus::RegisterSerializer<dmq::util::ThreadStatsPacket>("ThreadStats", serializer);
    dmq::databus::DataBus::RegisterStringifier<dmq::util::ThreadStatsPacket>("ThreadStats", dmq::util::ThreadStatsPacketToString);

    // Subscribe to ThreadStats. Asynchronous delivery to NodeBridge thread.
    instance.threadStatsConn = dmq::databus::DataBus::Subscribe<dmq::util::ThreadStatsPacket>("ThreadStats", [](const dmq::util::ThreadStatsPacket& packet) {
        auto& inst = GetInstance();
        std::ostringstream oss(std::ios::binary);
        static dmq::util::ThreadStatsPacketSerializer ser;
        ser.Write(oss, packet);
        if (oss.good()) {
            std::string buf = oss.str();
            inst.telemetrySocket.Send(buf.data(), buf.size());
        }
    }, instance.thread.get());

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

    // Setup periodic heartbeat timer
    instance.timerConn = instance.heartbeatTimer.OnExpired.Connect(dmq::MakeDelegate(SendHeartbeat, *instance.thread));
    instance.heartbeatTimer.Start(std::chrono::milliseconds(HEARTBEAT_INTERVAL_MS));
}

void NodeBridge::SendHeartbeat() {
    auto& instance = GetInstance();
    
    dmq::NodeInfoPacket packet;
    packet.nodeId        = instance.nodeId;
    packet.hostname      = instance.hostname;
    packet.ipAddress     = instance.ipAddress;
    packet.totalMsgCount = instance.totalMsgCount.load();
    packet.uptimeMs      = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - instance.startTime).count());

    std::string joined;
    for (const auto& t : instance.topics) {
        if (!joined.empty()) joined += ';';
        joined += t;
    }
    packet.topicsStr = std::move(joined);

    static serialize ms;
    std::ostringstream oss(std::ios::binary);
    ms.write(oss, packet);

    if (oss.good()) {
        std::string buf = oss.str();
        instance.telemetrySocket.Send(buf.data(), buf.size());
    }
}

void NodeBridge::Stop() {
    auto& instance = GetInstance();
    if (!instance.thread) return;

    instance.monitorConn.Disconnect();
    instance.threadStatsConn.Disconnect();
    instance.timerConn.Disconnect();
    
    instance.thread->ExitThread();
    instance.thread.reset();
    instance.telemetrySocket.Close();
}
