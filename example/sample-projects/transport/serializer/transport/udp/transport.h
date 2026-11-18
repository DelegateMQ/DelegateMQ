#ifndef TRANSPORT_H
#define TRANSPORT_H

/// @file transport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief UDP transport (Linux or Win32 sockets, picked by the build).
///
/// UDP can lose messages, so this transport is used with acknowledgments:
/// the sender wraps it in a ReliableTransport (see sender.h) that resends a
/// message until the receiver acknowledges it.

#include "DelegateMQ.h"

/// The transport class: dmq::transport::LinuxUdpTransport or Win32UdpTransport
using SampleTransport = dmq::transport::UdpTransport;

/// 1: the receiver acknowledges each message, and the sender retries
/// unacknowledged ones (TransportMonitor, RetryMonitor, ReliableTransport).
#define SAMPLE_ACKS 1

/// 1: create the Sender before the Receiver.
#define SAMPLE_SENDER_FIRST 0

inline int CreateSenderTransport(SampleTransport& transport)
{
    return transport.Create(SampleTransport::Type::PUB, "127.0.0.1", 8080);
}

inline int CreateReceiverTransport(SampleTransport& transport)
{
    return transport.Create(SampleTransport::Type::SUB, "127.0.0.1", 8080);
}

#endif
