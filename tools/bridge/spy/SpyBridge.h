// SpyBridge.h
// @see https://github.com/DelegateMQ/DelegateMQ
// Desktop front end of dmq::databus::SpyReporter: this process's DataBus traffic
// to dmq-spy over UDP, unicast or multicast.

#ifndef SPY_BRIDGE_H
#define SPY_BRIDGE_H

#include "DelegateMQ.h"
#include "extras/databus/SpyReporter.h"
#include "../../net/UdpSocketTransport.h"
#include <string>
#include <memory>

/// @brief Static wrapper around the library's dmq::databus::SpyReporter for
/// desktop processes. The library bridge does the work (DataBus::Monitor(),
/// its own DROP-policy thread, a SpyPacket framed with SPY_REMOTE_ID per
/// message); this adds a UDP socket that can also send multicast, which the
/// library's desktop UDP transports don't configure. An embedded target uses
/// dmq::databus::SpyReporter directly with its port's UDP transport.
class SpyBridge {
public:
    /// @brief Start the spy bridge using Unicast UDP.
    /// @param address The IP address of the Spy Console (viewer).
    /// @param port The UDP port to send traffic to.
    /// @param nodeId Unique identifier for this node.
    static void Start(const std::string& address, uint16_t port, const std::string& nodeId = "");

    /// @brief Start the spy bridge using Multicast UDP.
    /// @param groupAddr The Multicast group address (e.g. 239.0.0.1).
    /// @param port The UDP port to send traffic to.
    /// @param nodeId Unique identifier for this node.
    /// @param localInterface The local interface to use (e.g. 192.168.0.123).
    static void StartMulticast(const std::string& groupAddr, uint16_t port, const std::string& nodeId = "", const std::string& localInterface = "0.0.0.0");

    /// @brief Stop the spy bridge and cleanup resources.
    static void Stop();

    /// @brief Messages dropped so far (queue full or send failed); 0 if not started.
    static uint32_t GetDropped();

private:
    SpyBridge() = default;
    ~SpyBridge() = default;

    enum class TransportType { UNICAST, MULTICAST };

    static void Init(const std::string& address, uint16_t port, TransportType type, const std::string& nodeId, const std::string& localInterface = "");

    struct Instance {
        std::unique_ptr<dmq::databus::SpyReporter> bridge;
        UdpSocketTransport transport;
    };

    static Instance& GetInstance() {
        static Instance instance;
        return instance;
    }
};

#endif // SPY_BRIDGE_H
