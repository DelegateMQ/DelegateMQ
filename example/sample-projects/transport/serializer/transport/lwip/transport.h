#ifndef TRANSPORT_H
#define TRANSPORT_H

/// @file transport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief UDP over the lwIP stack's socket API (ArmLwipUdpTransport), on the host.
///
/// The same transport an embedded target uses with lwIP, run here on lwIP's
/// own Unix port (contrib/ports/unix: lwIP threads are pthreads) and its
/// built-in loopback interface (127.0.0.1). Datagrams go through lwIP's
/// UDP/IP code, not the host's network stack. lwIP's options are in
/// lwipopts.h, next to this file; lwip_init.h starts lwIP.

#include "DelegateMQ.h"
#include "lwip_init.h"

/// dmq::transport::UdpTransport from ArmLwipUdpTransport.h
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
