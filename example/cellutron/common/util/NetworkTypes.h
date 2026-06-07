#ifndef CELLUTRON_NETWORK_TYPES_H
#define CELLUTRON_NETWORK_TYPES_H

/// @brief Platform-specific NetworkNode type alias for the cellutron example.
///
/// Selects the correct UDP transport for the current platform and defines
/// `cellutron::Network` as a ready-to-use NetworkNode specialization.
/// Each node's System.h declares `cellutron::Network m_network;` — no other
/// network infrastructure files needed.

#include "extras/databus/NetworkNode.h"

#if defined(_WIN32) || defined(_WIN64)
    #include "port/transport/win32-udp/Win32UdpTransport.h"
    namespace cellutron {
        using Network = dmq::databus::NetworkNode<dmq::transport::Win32UdpTransport>;
    }
#elif defined(__linux__)
    #include "port/transport/linux-udp/LinuxUdpTransport.h"
    namespace cellutron {
        using Network = dmq::databus::NetworkNode<dmq::transport::LinuxUdpTransport>;
    }
#else
    #error "NetworkTypes.h: no UDP transport available for this platform."
#endif

#endif // CELLUTRON_NETWORK_TYPES_H
