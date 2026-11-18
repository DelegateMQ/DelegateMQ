#ifndef TRANSPORT_H
#define TRANSPORT_H

/// @file transport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief TCP transport (Linux or Win32 sockets, picked by the build).
///
/// The receiver is the TCP server and the sender the client, so the receiver
/// is created first (the client connects when it is created). Acknowledgments
/// are on as with UDP: a TCP connection can still drop, and the sample shows
/// the same retry machinery on both.

#include "DelegateMQ.h"

#if defined(_WIN32)
using SampleTransport = dmq::transport::Win32TcpTransport;
#else
using SampleTransport = dmq::transport::LinuxTcpTransport;
#endif

/// 1: the receiver acknowledges each message, and the sender retries
/// unacknowledged ones (TransportMonitor, RetryMonitor, ReliableTransport).
#define SAMPLE_ACKS 1

/// 1: create the Sender before the Receiver.
#define SAMPLE_SENDER_FIRST 0

inline int CreateSenderTransport(SampleTransport& transport)
{
    // Connects to the receiver's server socket
    return transport.Create(SampleTransport::Type::CLIENT, "127.0.0.1", 8080);
}

inline int CreateReceiverTransport(SampleTransport& transport)
{
    return transport.Create(SampleTransport::Type::SERVER, "127.0.0.1", 8080);
}

#endif
