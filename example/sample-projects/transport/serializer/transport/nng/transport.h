#ifndef TRANSPORT_H
#define TRANSPORT_H

/// @file transport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief NNG (nanomsg-next-gen) transport (PUB/SUB sockets over TCP).

#include "DelegateMQ.h"

using SampleTransport = dmq::transport::NngTransport;

/// 0: no acknowledgments; a PUB socket has no return path for them.
#define SAMPLE_ACKS 0

/// 1: create the Sender (PUB, listens) before the Receiver (SUB): the SUB's
/// dial fails if nothing is listening yet.
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
