#ifndef TRANSPORT_H
#define TRANSPORT_H

/// @file transport.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Shared-memory transport (ShmRingTransport): Win32ShmTransport (file
/// mapping + named events) on Windows, LinuxShmTransport (shm_open + named
/// semaphores) on Linux.
///
/// The Sender is side A: it creates the shared region, so it is created
/// first. The Receiver, side B, attaches to it. Shared memory doesn't lose
/// messages, but acknowledgments are on to show that a TransportMonitor works
/// over it too (in a real link they detect a hung or crashed peer).
///
/// Both ends here poll on a timer (the Receiver every 50 ms, the Sender for
/// ACKs after each send), so Receive() is made non-blocking. A dedicated
/// receive thread would keep the default timeout and sleep until the peer's
/// event or semaphore wakes it; see transport/shm-ring.

#include "DelegateMQ.h"
#if defined(_WIN32)
#include "port/transport/win32-shm/Win32ShmTransport.h"
#else
#include "port/transport/linux-shm/LinuxShmTransport.h"
#endif

/// 16 slots of 1 KB each way: room for the most verbose serializer's message
/// (RapidJSON) and the ACKs. Both ends of a link must use the same sizes.
#if defined(_WIN32)
using SampleTransport = dmq::transport::Win32ShmTransport<16, 1024>;
#else
using SampleTransport = dmq::transport::LinuxShmTransport<16, 1024>;
#endif

/// 1: the receiver acknowledges each message, and the sender retries
/// unacknowledged ones (TransportMonitor, RetryMonitor, ReliableTransport).
#define SAMPLE_ACKS 1

/// 1: create the Sender before the Receiver (side A sets up the region).
#define SAMPLE_SENDER_FIRST 1

/// Link name: Local\dmq-shm-serializer-sample (Windows),
/// /dev/shm/dmq-shm-serializer-sample (Linux)
#define SAMPLE_SHM_NAME "serializer-sample"

inline int CreateSenderTransport(SampleTransport& transport)
{
    const int err = transport.Create(SAMPLE_SHM_NAME, SampleTransport::Side::A);
    transport.SetRecvTimeout(std::chrono::milliseconds(0));
    return err;
}

inline int CreateReceiverTransport(SampleTransport& transport)
{
    const int err = transport.Create(SAMPLE_SHM_NAME, SampleTransport::Side::B);
    transport.SetRecvTimeout(std::chrono::milliseconds(0));
    return err;
}

#endif
