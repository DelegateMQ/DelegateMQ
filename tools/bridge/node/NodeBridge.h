// NodeBridge.h
// @see https://github.com/DelegateMQ/DelegateMQ
// Opt-in node heartbeat bridge for the DelegateMQ Node Monitor.
//
// Usage (mirrors SpyBridge pattern — zero cost if never called):
//
//   NodeBridge::Start("MyNode", "127.0.0.1", 9998);   // unicast
//   NodeBridge::StartMulticast("MyNode", "239.1.1.1", 9998);  // multicast
//   ...
//   NodeBridge::Stop();
//
// Once started, NodeBridge subscribes to DataBus::Monitor to auto-discover
// published topics and count messages, then broadcasts a NodeInfoPacket
// heartbeat every HEARTBEAT_INTERVAL_MS milliseconds.
//
// Health: each heartbeat is followed by a NodeHealthPacket, shown in
// dmq-monitor's Health column. DataBus errors are counted automatically. Link
// health (ACK timeouts, abandoned messages, backlog limits) needs the objects
// that see it, which the application owns, so it is opt-in:
//
//   NodeBridge::Watch(m_network);            // a dmq::databus::NetworkNode (all peers)
//   NodeBridge::Watch(transportMonitor);     // or a TransportMonitor ...
//   NodeBridge::Watch(retryMonitor);         // ... and its RetryMonitor
//
// Watch() may be called before or after Start(). Stop() drops all watches; a
// watched object must stay alive until Stop() or it is destroyed itself.

#ifndef NODE_BRIDGE_H
#define NODE_BRIDGE_H

#include "DelegateMQ.h"
#include "NodeInfoPacket.h"
#include "../../net/UdpSocket.h"
#include <string>
#include <memory>
#include <atomic>
#include <set>
#include <chrono>
#include <mutex>
#include <vector>

class NodeBridge {
public:
    /// @brief Start unicast heartbeats to the monitor console.
    /// @param nodeId    User-defined name for this node (e.g. "SensorNode-1").
    /// @param address   IP address of the dmq-monitor console.
    /// @param port      UDP port the monitor is listening on (default: 9998).
    static void Start(const std::string& nodeId, const std::string& address, uint16_t port);

    /// @brief Start multicast heartbeats to the monitor console.
    /// @param nodeId         User-defined name for this node.
    /// @param groupAddr      Multicast group address (e.g. "239.1.1.1").
    /// @param port           UDP port the monitor is listening on (default: 9998).
    /// @param localInterface Local interface IP to send from (default: auto-detect).
    static void StartMulticast(const std::string& nodeId, const std::string& groupAddr,
                               uint16_t port, const std::string& localInterface = "0.0.0.0");

    /// @brief Stop the bridge and clean up resources. Also drops all Watch() connections.
    static void Stop();

    /// @brief Report a TransportMonitor's ACK timeouts and limits in the health packet.
    static void Watch(dmq::util::TransportMonitor& monitor);

    /// @brief Report a RetryMonitor's abandoned messages in the health packet.
    static void Watch(dmq::util::RetryMonitor& monitor);

    /// @brief Report a dmq::databus::NetworkNode's link health, for all its peers.
    /// @details A template so this header doesn't need NetworkNode.h; it only
    /// compiles in code that has a NetworkNode to pass.
    template <typename NetworkNodeT>
    static void Watch(NetworkNodeT& node) {
        using Status = dmq::util::TransportMonitor::Status;
        auto& inst = GetInstance();
        const std::lock_guard<std::mutex> lock(inst.healthMutex);
        inst.sources |= dmq::NodeHealthPacket::SRC_TRANSPORT | dmq::NodeHealthPacket::SRC_RETRY;
        inst.watchConns.push_back(node.OnPeerSendStatus.Connect(dmq::MakeDelegate(
            [](const dmq::xstring& peer, dmq::DelegateRemoteId id, uint16_t seq, Status status) {
                if (status == Status::TIMEOUT) RecordTimeout(peer.c_str(), id, seq);
            })));
        inst.watchConns.push_back(node.OnDeliveryFailed.Connect(dmq::MakeDelegate(
            [](const dmq::xstring& peer, dmq::DelegateRemoteId id, uint16_t seq) {
                RecordDeliveryFailure(peer.c_str(), id, seq);
            })));
        inst.watchConns.push_back(node.OnPeerCapExceeded.Connect(dmq::MakeDelegate(
            [](const dmq::xstring& peer, size_t count) { RecordCapExceeded(peer.c_str(), count); })));
        inst.watchConns.push_back(node.OnPeerPendingExceeded.Connect(dmq::MakeDelegate(
            [](const dmq::xstring& peer, size_t remaining) { RecordPendingExceeded(peer.c_str(), remaining); })));
    }

    /// @brief Interval between heartbeat packets in milliseconds.
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;

private:
    NodeBridge() = default;
    ~NodeBridge() = default;

    enum class TransportType { UNICAST, MULTICAST };

    static void InitTelemetry(const std::string& address, uint16_t port, bool isMulticast, const std::string& localInterface = "");
    static void SendHeartbeat();

    static void OnMonitorPacket(const dmq::databus::SpyPacket& packet);
    static void SendHealth();

    // Health counters. Called synchronously on whichever thread raises the
    // event; peer is "" when the source has no peer name.
    static void RecordDataBusError(const dmq::xstring& topic, dmq::DelegateError error);
    static void RecordTimeout(const char* peer, dmq::DelegateRemoteId id, uint16_t seq);
    static void RecordDeliveryFailure(const char* peer, dmq::DelegateRemoteId id, uint16_t seq);
    static void RecordCapExceeded(const char* peer, size_t count);
    static void RecordPendingExceeded(const char* peer, size_t remaining);
    static void SetLastEvent(const std::string& text);
    static void OnThreadStats(const dmq::util::ThreadStatsPacket& packet);
    static dmq::xstring OnThreadStatsStringify(const dmq::util::ThreadStatsPacket& packet);

    struct Instance {
        std::unique_ptr<dmq::os::Thread> thread;
        
        dmq::xstring nodeId;
        dmq::xstring hostname;
        dmq::xstring ipAddress;
        dmq::xstring address;
        dmq::xstring localInterface;
        uint16_t port = 0;
        TransportType type = TransportType::UNICAST;

        std::atomic<uint32_t> totalMsgCount{0};
        dmq::xset<dmq::xstring> topics;

        dmq::ScopedConnection monitorConn;
        dmq::ScopedConnection threadStatsConn;
        dmq::ScopedConnection timerConn;
        dmq::util::Timer heartbeatTimer;
        UdpSocket telemetrySocket;
        bool isMulticast = false;

        std::chrono::steady_clock::time_point startTime;

        // Health (see NodeHealthPacket)
        std::atomic<uint32_t> dataBusErrors{0};
        std::atomic<uint32_t> sendTimeouts{0};
        std::atomic<uint32_t> deliveryFailures{0};
        std::atomic<uint32_t> capExceeded{0};
        std::atomic<uint32_t> pendingExceeded{0};
        std::mutex healthMutex;                         // guards sources, lastEvent, watchConns
        uint32_t sources = dmq::NodeHealthPacket::SRC_DATABUS;
        std::string lastEvent;
        std::vector<dmq::ScopedConnection> watchConns;
        dmq::ScopedConnection dataBusErrorConn;
    };

    static Instance& GetInstance() {
        static Instance instance;
        return instance;
    }
};

#endif // NODE_BRIDGE_H
