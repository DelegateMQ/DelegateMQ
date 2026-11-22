#ifndef TRANSPORT_H
#define TRANSPORT_H

/// @file transport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief ZeroMQ transport (PUB/SUB sockets over TCP).

#include "DelegateMQ.h"

using SampleTransport = dmq::transport::ZeroMqTransport;

/// 0: no acknowledgments. A PUB socket has no return path for them, and
/// ZeroMQ queues and reconnects on its own.
#define SAMPLE_ACKS 0

/// 1: create the Sender (PUB, binds) before the Receiver (SUB, connects).
#define SAMPLE_SENDER_FIRST 1

inline int CreateSenderTransport(SampleTransport& transport)
{
    return transport.Create(SampleTransport::Type::PUB, "tcp://*:5555");
}

inline int CreateReceiverTransport(SampleTransport& transport)
{
    return transport.Create(SampleTransport::Type::SUB, "tcp://localhost:5555");
}

#endif
