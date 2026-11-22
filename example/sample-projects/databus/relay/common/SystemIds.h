#ifndef SYSTEM_IDS_H
#define SYSTEM_IDS_H

#include "DelegateMQ.h"

// Topology:  sensor <--- UDP ---> gateway <--- local IPC ---> monitor
//   local IPC = Win32 named pipe (Windows) or Unix domain socket (Linux)
namespace SystemTopic {
    // Topic names
    inline constexpr const char* const SensorTemp  = "sensor/temp";   // sensor -> gateway -> monitor
    inline constexpr const char* const CmdSetpoint = "cmd/setpoint";  // monitor -> gateway -> sensor

    // Remote IDs (DelegateRemoteId). Each hop is an independent link, so an ID
    // only has to match between the two ends of one hop; this sample reuses
    // the same ID per topic on both hops for simplicity.
    enum RemoteId : dmq::DelegateRemoteId {
        SensorTempId  = 100,
        CmdSetpointId = 101
    };
}

namespace SystemLink {
    // Ethernet hop (UDP on loopback for the demo)
    inline constexpr const char* const UdpAddr = "127.0.0.1";
    inline constexpr unsigned short GatewayUdpPort = 5020;   // gateway listens: sensor/temp from sensor
    inline constexpr unsigned short SensorUdpPort  = 5021;   // sensor listens: cmd/setpoint from gateway

    // Local IPC hop (gateway <-> monitor).
    // Windows: a named pipe. gateway = pipe server (creates it), monitor = pipe client.
    inline constexpr const char* const PipeName = "\\\\.\\pipe\\dmq_relay";
    // Linux: a Unix domain datagram socket. Each end binds its own path and
    // sends to the other's (sun_path holds at most 107 chars + NUL).
    inline constexpr const char* const GatewaySocketPath = "/tmp/dmq_relay_gateway.sock";
    inline constexpr const char* const MonitorSocketPath = "/tmp/dmq_relay_monitor.sock";

    // Short name of the local IPC hop, for log output.
#ifdef _WIN32
    inline constexpr const char* const IpcName = "pipe";
#else
    inline constexpr const char* const IpcName = "socket";
#endif
}

#endif // SYSTEM_IDS_H
