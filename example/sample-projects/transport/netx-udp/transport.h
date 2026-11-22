#ifndef TRANSPORT_H
#define TRANSPORT_H

/// @file transport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief UDP over NetX Duo (NetXUdpTransport) on ThreadX, for the
/// transport/serializer app (sender.h, receiver.h), which this sample reuses.
///
/// NetXUdpTransport takes the NetX IP instance and packet pool to use, which
/// main_delegate.cpp creates before the Sender and Receiver. Both ends talk to
/// 127.0.0.1 through NetX Duo's own loopback interface.

#include "DelegateMQ.h"

/// The NetX IP instance and packet pool (main_delegate.cpp)
extern NX_IP g_netxIp;
extern NX_PACKET_POOL g_netxPool;

/// NetXUdpTransport bound to this sample's IP instance and packet pool, so the
/// app can construct it with no arguments like the other transports.
class SampleTransport : public dmq::transport::NetXUdpTransport
{
public:
    SampleTransport() : NetXUdpTransport(&g_netxIp, &g_netxPool) {}
};

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
