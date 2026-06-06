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
    instance.nodeId = nodeId.c_str();
    instance.type = TransportType::UNICAST;
    InitTelemetry(address, port, false);
}

void NodeBridge::StartMulticast(const std::string& nodeId, const std::string& groupAddr, uint16_t port, const std::string& localInterface) {
    auto& instance = GetInstance();
    instance.nodeId = nodeId.c_str();
    instance.type = TransportType::MULTICAST;
    InitTelemetry(groupAddr, port, true, localInterface);
}

void NodeBridge::InitTelemetry(const std::string& address, uint16_t port, bool isMulticast, const std::string& localInterface) {
    auto& instance = GetInstance();
    if (instance.thread) return;

    instance.address = address.c_str();
    instance.port = port;
    instance.localInterface = localInterface.c_str();
    instance.isMulticast = isMulticast;
    instance.startTime = std::chrono::steady_clock::now();

    // Get Hostname
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) instance.hostname = hostname;

    // Create a dedicated bridge thread
    instance.thread = std::make_unique<dmq::os::Thread>("NodeBridge", 100, dmq::os::FullPolicy::DROP);
    instance.thread->CreateThread();

    // Subscribe to DataBus::Monitor to auto-discover topics. Asynchronous delivery to NodeBridge thread.
    instance.monitorConn = dmq::databus::DataBus::Monitor(dmq::MakeDelegate(&OnMonitorPacket), instance.thread.get());

    static dmq::util::ThreadStatsPacketSerializer serializer;
    dmq::databus::DataBus::RegisterSerializer<dmq::util::ThreadStatsPacket>("ThreadStats", serializer);
    dmq::databus::DataBus::RegisterStringifier<dmq::util::ThreadStatsPacket>("ThreadStats", dmq::MakeDelegate(&OnThreadStatsStringify));

    // Subscribe to ThreadStats. Asynchronous delivery to NodeBridge thread.
    instance.threadStatsConn = dmq::databus::DataBus::Subscribe<dmq::util::ThreadStatsPacket>("ThreadStats", dmq::MakeDelegate(&OnThreadStats), instance.thread.get());

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

void NodeBridge::OnMonitorPacket(const dmq::databus::SpyPacket& packet) {
    auto& inst = GetInstance();
    inst.totalMsgCount++;
    inst.topics.insert(packet.topic);
}

void NodeBridge::OnThreadStats(const dmq::util::ThreadStatsPacket& packet) {
    auto& inst = GetInstance();
    dmq::xostringstream oss(std::ios::binary);
    static dmq::util::ThreadStatsPacketSerializer ser;
    ser.Write(oss, packet);
    if (oss.good()) {
        const dmq::xstring& body = oss.str();
        dmq::xstring buf;
        buf.reserve(1 + body.size());
        buf.push_back(static_cast<char>(dmq::PacketType::ThreadStats));
        buf.append(body);
        inst.telemetrySocket.Send(buf.data(), buf.size());
    }
}

dmq::xstring NodeBridge::OnThreadStatsStringify(const dmq::util::ThreadStatsPacket& packet) {
    return dmq::util::ThreadStatsPacketToString(packet);
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

    dmq::xstring joined;
    for (const auto& t : instance.topics) {
        if (!joined.empty()) joined += ';';
        joined += t;
    }
    packet.topicsStr = joined.c_str();

    serialize ms;
    dmq::xostringstream oss(std::ios::binary);
    ms.write(oss, packet);

    if (oss.good()) {
        const dmq::xstring& body = oss.str();
        dmq::xstring buf;
        buf.reserve(1 + body.size());
        buf.push_back(static_cast<char>(dmq::PacketType::NodeInfo));
        buf.append(body);
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
