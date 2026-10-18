// NodeBridge.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Opt-in node heartbeat bridge implementation.

#include "NodeBridge.h"
#include "port/serialize/serialize/msg_serialize.h"
#include "extras/util/Timer.h"
#include "extras/util/ThreadMonitor.h"
#include "extras/util/ThreadMonitorSer.h"
#include "extras/util/NetworkConnect.h"
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

    // Health counters are totals since Start()
    instance.dataBusErrors = 0;
    instance.sendTimeouts = 0;
    instance.deliveryFailures = 0;
    instance.capExceeded = 0;
    instance.pendingExceeded = 0;
    {
        const std::lock_guard<std::mutex> lock(instance.healthMutex);
        instance.lastEvent.clear();
    }

    // Get Hostname
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) instance.hostname = hostname;

    // Self-reported address: the multicast interface if one was given, else the
    // first non-loopback IPv4 address. dmq-monitor keys its rows on nodeId + this,
    // so same-named nodes on different machines get separate rows.
    instance.ipAddress = (isMulticast && !localInterface.empty() && localInterface != "0.0.0.0")
        ? localInterface.c_str()
        : dmq::util::NetworkContext::GetLocalAddress().c_str();

    // Create a dedicated bridge thread
    instance.thread = std::make_unique<dmq::os::Thread>("NodeBridge", 100, dmq::os::FullPolicy::DROP);
    instance.thread->CreateThread();

    // Subscribe to DataBus::Monitor to auto-discover topics. Asynchronous delivery to NodeBridge thread.
    instance.monitorConn = dmq::databus::DataBus::Monitor(dmq::MakeDelegate(&OnMonitorPacket), instance.thread.get());

    static dmq::util::ThreadStatsPacketSerializer serializer;
    dmq::databus::DataBus::RegisterSerializer<dmq::util::ThreadStatsPacket>("ThreadStats", serializer);
    dmq::databus::DataBus::RegisterStringifier<dmq::util::ThreadStatsPacket>("ThreadStats", dmq::MakeDelegate(&OnThreadStatsStringify));

    // DataBus errors count toward health on every node, with no app code.
    instance.dataBusErrorConn = dmq::databus::DataBus::SubscribeError(dmq::MakeDelegate(&RecordDataBusError));

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

    SendHealth();
}

void NodeBridge::SendHealth() {
    auto& instance = GetInstance();

    dmq::NodeHealthPacket packet;
    packet.nodeId           = instance.nodeId;
    packet.ipAddress        = instance.ipAddress;
    packet.dataBusErrors    = instance.dataBusErrors.load();
    packet.sendTimeouts     = instance.sendTimeouts.load();
    packet.deliveryFailures = instance.deliveryFailures.load();
    packet.capExceeded      = instance.capExceeded.load();
    packet.pendingExceeded  = instance.pendingExceeded.load();
    {
        const std::lock_guard<std::mutex> lock(instance.healthMutex);
        packet.sources   = instance.sources;
        packet.lastEvent = instance.lastEvent.c_str();
    }

    serialize ms;
    dmq::xostringstream oss(std::ios::binary);
    ms.write(oss, packet);

    if (oss.good()) {
        const dmq::xstring& body = oss.str();
        dmq::xstring buf;
        buf.reserve(1 + body.size());
        buf.push_back(static_cast<char>(dmq::PacketType::Health));
        buf.append(body);
        instance.telemetrySocket.Send(buf.data(), buf.size());
    }
}

void NodeBridge::Watch(dmq::util::TransportMonitor& monitor) {
    using Status = dmq::util::TransportMonitor::Status;
    auto& inst = GetInstance();
    const std::lock_guard<std::mutex> lock(inst.healthMutex);
    inst.sources |= dmq::NodeHealthPacket::SRC_TRANSPORT;
    inst.watchConns.push_back(monitor.OnSendStatus.Connect(dmq::MakeDelegate(
        [](dmq::DelegateRemoteId id, uint16_t seq, Status status) {
            if (status == Status::TIMEOUT) RecordTimeout("", id, seq);
        })));
    inst.watchConns.push_back(monitor.OnCapExceeded.Connect(dmq::MakeDelegate(
        [](size_t count) { RecordCapExceeded("", count); })));
    inst.watchConns.push_back(monitor.OnPendingExceeded.Connect(dmq::MakeDelegate(
        [](size_t remaining) { RecordPendingExceeded("", remaining); })));
}

void NodeBridge::Watch(dmq::util::RetryMonitor& monitor) {
    auto& inst = GetInstance();
    const std::lock_guard<std::mutex> lock(inst.healthMutex);
    inst.sources |= dmq::NodeHealthPacket::SRC_RETRY;
    inst.watchConns.push_back(monitor.OnDeliveryFailed.Connect(dmq::MakeDelegate(
        [](dmq::DelegateRemoteId id, uint16_t seq) { RecordDeliveryFailure("", id, seq); })));
}

static std::string PeerPrefix(const char* peer) {
    return (peer && *peer) ? std::string(" ") + peer : std::string();
}

void NodeBridge::SetLastEvent(const std::string& text) {
    auto& inst = GetInstance();
    const std::lock_guard<std::mutex> lock(inst.healthMutex);
    inst.lastEvent = text;
}

void NodeBridge::RecordDataBusError(const dmq::xstring& topic, dmq::DelegateError error) {
    GetInstance().dataBusErrors++;
    SetLastEvent("DataBus error " + std::to_string(static_cast<int>(error)) + " on " + topic.c_str());
}

void NodeBridge::RecordTimeout(const char* peer, dmq::DelegateRemoteId id, uint16_t seq) {
    GetInstance().sendTimeouts++;
    SetLastEvent("ACK timeout," + PeerPrefix(peer) + " id=" + std::to_string(id) + " seq=" + std::to_string(seq) + ", retrying");
}

void NodeBridge::RecordDeliveryFailure(const char* peer, dmq::DelegateRemoteId id, uint16_t seq) {
    GetInstance().deliveryFailures++;
    SetLastEvent("delivery failed," + PeerPrefix(peer) + " id=" + std::to_string(id) + " seq=" + std::to_string(seq) + ", retries exhausted");
}

void NodeBridge::RecordCapExceeded(const char* peer, size_t count) {
    GetInstance().capExceeded++;
    SetLastEvent("send rejected," + PeerPrefix(peer) + " " + std::to_string(count) + " messages unacknowledged");
}

void NodeBridge::RecordPendingExceeded(const char* peer, size_t remaining) {
    GetInstance().pendingExceeded++;
    SetLastEvent("retry backlog," + PeerPrefix(peer) + " " + std::to_string(remaining) + " expired messages waiting");
}

void NodeBridge::Stop() {
    auto& instance = GetInstance();
    if (!instance.thread) return;

    instance.monitorConn.Disconnect();
    instance.threadStatsConn.Disconnect();
    instance.timerConn.Disconnect();
    instance.dataBusErrorConn.Disconnect();
    {
        const std::lock_guard<std::mutex> lock(instance.healthMutex);
        instance.watchConns.clear();    // ScopedConnection disconnects on destruction
        instance.sources = dmq::NodeHealthPacket::SRC_DATABUS;
    }
    
    instance.thread->ExitThread();
    instance.thread.reset();
    instance.telemetrySocket.Close();
}
