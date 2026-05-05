// SpyBridge.h
// @see https://github.com/DelegateMQ/DelegateMQ
// Asynchronous DataBus monitoring bridge interface.

#ifndef SPY_BRIDGE_H
#define SPY_BRIDGE_H

#include "DelegateMQ.h"
#include "../src/UdpSocket.h"
#include <string>
#include <memory>

class SpyBridge {
public:
    /// @brief Start the spy bridge using Unicast UDP.
    /// @param address The IP address of the Spy Console (viewer).
    /// @param port The UDP port to send traffic to.
    static void Start(const std::string& address, uint16_t port);

    /// @brief Start the spy bridge using Multicast UDP.
    /// @param groupAddr The Multicast group address (e.g. 239.0.0.1).
    /// @param port The UDP port to send traffic to.
    /// @param localInterface The local interface to use (e.g. 192.168.0.123).
    static void StartMulticast(const std::string& groupAddr, uint16_t port, const std::string& localInterface = "0.0.0.0");

    /// @brief Stop the spy bridge and cleanup resources.
    static void Stop();

private:
    SpyBridge() = default;
    ~SpyBridge() = default;

    enum class TransportType { UNICAST, MULTICAST };

    static void Init(const std::string& address, uint16_t port, TransportType type, const std::string& localInterface = "");

    struct Instance {
        std::unique_ptr<dmq::os::Thread> thread;
        dmq::ScopedConnection monitorConn;
        UdpSocket telemetrySocket;
        std::string address;
        std::string localInterface;
        uint16_t port = 0;
        TransportType type = TransportType::UNICAST;
    };

    static Instance& GetInstance() {
        static Instance instance;
        return instance;
    }
};

#endif // SPY_BRIDGE_H
