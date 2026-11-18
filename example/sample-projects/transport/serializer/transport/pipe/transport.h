#ifndef TRANSPORT_H
#define TRANSPORT_H

/// @file transport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Windows named pipe transport (Windows only).

#include "DelegateMQ.h"

using SampleTransport = dmq::transport::Win32PipeTransport;

/// 0: no acknowledgments; a pipe is a reliable local connection.
#define SAMPLE_ACKS 0

/// 1: create the Sender before the Receiver.
#define SAMPLE_SENDER_FIRST 0

inline int CreateSenderTransport(SampleTransport& transport)
{
    return transport.Create(SampleTransport::Type::PUB, TEXT("\\\\.\\pipe\\pipedelegate"));
}

inline int CreateReceiverTransport(SampleTransport& transport)
{
    return transport.Create(SampleTransport::Type::SUB, TEXT("\\\\.\\pipe\\pipedelegate"));
}

#endif
