#ifndef TRANSPORT_H
#define TRANSPORT_H

/// @file transport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief UDP over the lwIP stack's netconn API (ArmLwipNetconnUdpTransport),
/// on the host.
///
/// As the lwip build (same lwIP setup: transport/lwip/lwipopts.h and
/// lwip_init.h), but through lwIP's netconn API instead of its socket API.

#include "DelegateMQ.h"
#include "lwip_init.h"

/// dmq::transport::NetconnUdpTransport from ArmLwipNetconnUdpTransport.h
using SampleTransport = dmq::transport::NetconnUdpTransport;

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
