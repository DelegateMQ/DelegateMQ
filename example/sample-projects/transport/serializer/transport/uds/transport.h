#ifndef TRANSPORT_H
#define TRANSPORT_H

/// @file transport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Unix domain datagram socket transport (Linux).
///
/// Each end binds its own socket path and sends to the other's. Unix datagram
/// sockets are reliable and ordered, so no acknowledgments are used (as with
/// the Windows pipe). Either end may start first.

#include "DelegateMQ.h"

using SampleTransport = dmq::transport::LinuxUdsTransport;

/// 1: the receiver acknowledges each message, and the sender retries
/// unacknowledged ones (TransportMonitor, RetryMonitor, ReliableTransport).
#define SAMPLE_ACKS 0

/// 1: create the Sender before the Receiver.
#define SAMPLE_SENDER_FIRST 0

/// Socket paths. /tmp suits a demo; use a per-user directory such as
/// $XDG_RUNTIME_DIR when other local users mustn't reach the socket.
#define SAMPLE_UDS_SENDER   "/tmp/dmq-transport-sender.sock"
#define SAMPLE_UDS_RECEIVER "/tmp/dmq-transport-receiver.sock"

inline int CreateSenderTransport(SampleTransport& transport)
{
    return transport.Create(SampleTransport::Type::PUB, SAMPLE_UDS_SENDER, SAMPLE_UDS_RECEIVER);
}

inline int CreateReceiverTransport(SampleTransport& transport)
{
    return transport.Create(SampleTransport::Type::SUB, SAMPLE_UDS_RECEIVER, SAMPLE_UDS_SENDER);
}

#endif
