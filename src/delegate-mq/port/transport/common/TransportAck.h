#ifndef DMQ_TRANSPORT_ACK_H
#define DMQ_TRANSPORT_ACK_H

/// @file TransportAck.h
/// @see https://github.com/DelegateMQ/DelegateMQ
///
/// @brief The ACK step every transport runs after it receives a message, in
/// one place so the transports can't drift apart:
/// - A received ACK clears its sequence number in the TransportMonitor, if one
///   is set (the sender's RetryMonitor stops retrying it).
/// - Any other message is ACKed back whenever a send path exists, whether or
///   not this transport tracks its own sends: a receive-only transport (e.g.
///   NetworkNode's SUB) has no monitor but must still ACK the sender's
///   RELIABLE messages.
///
/// Most transports ACK through an ITransport (AckReceived()). One that must
/// write the ACK to a particular peer itself (a UDP sendto() back to the
/// sender, a TCP server's client socket) calls HandleAck() and writes
/// EncodeAck()'s bytes.

#include "DmqHeader.h"
#include "ITransport.h"
#include "ITransportMonitor.h"
#include "../../../delegate/IDispatcher.h"
#include <cstdint>

namespace dmq::transport {

/// If header is an ACK, clear its sequence number in monitor (if any).
/// @return true if it was an ACK: nothing more to do with it
inline bool HandleAck(const DmqHeader& header, ITransportMonitor* monitor)
{
    if (header.GetId() != dmq::ACK_REMOTE_ID)
        return false;
    if (monitor)
        monitor->Remove(header.GetSeqNum());
    return true;
}

/// Send the ACK for sequence number seqNum through transport: an empty
/// message with id ACK_REMOTE_ID. A failed send is the transport's to report;
/// the sender's TransportMonitor then times the message out.
inline void SendAck(ITransport& transport, uint16_t seqNum)
{
    const dmq::ByteWriter empty;
    const DmqHeader ack(dmq::ACK_REMOTE_ID, seqNum);
    transport.Send(empty, ack);
}

/// The whole ACK step for a transport that ACKs through an ITransport.
/// @param sendTransport the send path (often the transport itself); nullptr
/// to send no ACKs
inline void AckReceived(const DmqHeader& header, ITransportMonitor* monitor, ITransport* sendTransport)
{
    if (!HandleAck(header, monitor) && sendTransport)
        SendAck(*sendTransport, header.GetSeqNum());
}

/// The bytes of the ACK for seqNum (a header only), for a transport that
/// writes it to the peer itself.
inline void EncodeAck(uint16_t seqNum, char (&buf)[DmqHeader::HEADER_SIZE])
{
    const DmqHeader ack(dmq::ACK_REMOTE_ID, seqNum);
    ack.Encode(buf);
}

/// The same, into a byte buffer.
inline void EncodeAck(uint16_t seqNum, uint8_t (&buf)[DmqHeader::HEADER_SIZE])
{
    const DmqHeader ack(dmq::ACK_REMOTE_ID, seqNum);
    ack.Encode(buf);
}

} // namespace dmq::transport

#endif // DMQ_TRANSPORT_ACK_H
