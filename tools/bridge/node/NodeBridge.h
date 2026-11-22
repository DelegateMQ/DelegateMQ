// NodeBridge.h
// @see https://github.com/DelegateMQ/DelegateMQ
// Desktop front end of dmq::databus::NodeReporter: this process's heartbeat,
// health and thread statistics to dmq-monitor and dmq-thread over UDP.
//
// Usage (zero cost if never called):
//
//   NodeBridge::Start("MyNode", "127.0.0.1", 9998);           // unicast
//   NodeBridge::StartMulticast("MyNode", "239.1.1.1", 9998);  // multicast
//   ...
//   NodeBridge::Stop();
//
// dmq-monitor and dmq-thread both listen on port 9998 (multicast group
// 239.1.1.1): with both running, use StartMulticast(), or only one of them
// receives a unicast packet.
//
// Once started, the reporter sends a NodeInfoPacket heartbeat and a
// NodeHealthPacket every second (DMQ_NODE_REPORTER_HEARTBEAT_MS), and forwards
// the ThreadStatsPacket that dmq::util::ThreadMonitor publishes for each
// registered thread.
//
// Health: DataBus errors are counted automatically. Link health (ACK
// timeouts, abandoned messages, backlog limits) needs the objects that see
// it, which the application owns, so it is opt-in:
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
#include "extras/databus/NodeReporter.h"
#include "../../net/UdpSocketTransport.h"
#include <memory>
#include <string>

/// @brief Static wrapper around the library's dmq::databus::NodeReporter for
/// desktop processes: adds the hostname, this machine's address, and a UDP
/// socket that can send multicast. An embedded target uses NodeReporter
/// directly with its port's UDP transport.
class NodeBridge {
public:
    /// @brief Start unicast reporting to the monitor console.
    /// @param nodeId    User-defined name for this node (e.g. "SensorNode-1").
    /// @param address   IP address of the console.
    /// @param port      UDP port the console listens on (default: 9998).
    static void Start(const std::string& nodeId, const std::string& address, uint16_t port);

    /// @brief Start multicast reporting to the consoles.
    /// @param nodeId         User-defined name for this node.
    /// @param groupAddr      Multicast group address (e.g. "239.1.1.1").
    /// @param port           UDP port the consoles listen on (default: 9998).
    /// @param localInterface Local interface IP to send from (default: auto-detect).
    static void StartMulticast(const std::string& nodeId, const std::string& groupAddr,
                               uint16_t port, const std::string& localInterface = "0.0.0.0");

    /// @brief Stop reporting and clean up resources. Also drops all Watch() connections.
    static void Stop();

    /// @brief Report a TransportMonitor's ACK timeouts and limits in the health packet.
    static void Watch(dmq::util::TransportMonitor& monitor) { Reporter().Watch(monitor); }

    /// @brief Report a RetryMonitor's abandoned messages in the health packet.
    static void Watch(dmq::util::RetryMonitor& monitor) { Reporter().Watch(monitor); }

    /// @brief Report a dmq::databus::NetworkNode's link health, for all its peers.
    template <typename NetworkNodeT>
    static void Watch(NetworkNodeT& node) { Reporter().Watch(node); }

    /// @brief Messages dropped so far (queue full or send failed).
    static uint32_t GetDropped();

private:
    NodeBridge() = default;
    ~NodeBridge() = default;

    static void Init(const std::string& nodeId, const std::string& address, uint16_t port,
                     bool isMulticast, const std::string& localInterface);

    /// The reporter, created on first use (Watch() may come before Start())
    static dmq::databus::NodeReporter& Reporter();

    struct Instance {
        std::unique_ptr<dmq::databus::NodeReporter> reporter;
        UdpSocketTransport transport;
        bool started = false;
    };

    static Instance& GetInstance() {
        static Instance instance;
        return instance;
    }
};

#endif // NODE_BRIDGE_H
