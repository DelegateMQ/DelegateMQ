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

#ifndef NODE_BRIDGE_H
#define NODE_BRIDGE_H

#include "DelegateMQ.h"
#include "NodeInfoPacket.h"
#include "../src/UdpSocket.h"
#include <string>
#include <memory>
#include <atomic>
#include <set>
#include <chrono>

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

    /// @brief Stop the bridge and clean up resources.
    static void Stop();

    /// @brief Interval between heartbeat packets in milliseconds.
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;

private:
    NodeBridge() = default;
    ~NodeBridge() = default;

    enum class TransportType { UNICAST, MULTICAST };

    static void InitTelemetry(const std::string& address, uint16_t port, bool isMulticast, const std::string& localInterface = "");
    static void SendHeartbeat();

    struct Instance {
        std::unique_ptr<dmq::os::Thread> thread;
        
        std::string nodeId;
        std::string hostname;
        std::string ipAddress;
        std::string address;
        std::string localInterface;
        uint16_t port = 0;
        TransportType type = TransportType::UNICAST;

        std::atomic<uint32_t> totalMsgCount{0};
        std::set<std::string> topics;

        dmq::ScopedConnection monitorConn;
        dmq::ScopedConnection threadStatsConn;
        dmq::ScopedConnection timerConn;
        dmq::util::Timer heartbeatTimer;
        UdpSocket telemetrySocket;
        bool isMulticast = false;

        std::chrono::steady_clock::time_point startTime;
    };

    static Instance& GetInstance() {
        static Instance instance;
        return instance;
    }
};

#endif // NODE_BRIDGE_H
