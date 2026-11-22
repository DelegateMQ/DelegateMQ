#ifndef ITRANSPORT_H
#define ITRANSPORT_H

#include "DmqHeader.h"
#include "../../../delegate/DelegateOpt.h"

namespace dmq::transport {

/// @TODO Implement the ITransport interface if necessary.
/// @brief DelegateMQ transport interface. 
class ITransport
{
public:
    virtual ~ITransport() = default;
    /// Send data to a remote
    /// @param[in] os The bytes to send (os.data(), os.size()); empty for an ACK.
    /// @param[in] header The header to send.
    /// @return 0 if success.
    virtual int Send(const dmq::ByteWriter& os, const DmqHeader& header) = 0;

    /// Returned by Receive() when nothing arrived within the transport's receive
    /// timeout (or, for a non-blocking transport, nothing was waiting). Not an
    /// error: callers poll again.
    static constexpr int RECEIVE_NO_DATA = 1;

    /// Receive data from a remote
    /// @param[out] is Set to view the received payload, not including the header
    /// (`is.assign()`). The bytes live in the transport's receive buffer: valid
    /// until the next Receive() or Close() on this transport, so read them (or
    /// copy them) before that. No copy is made.
    /// @param[out] header Incoming delegate message header.
    /// @return 0 if a message was received, RECEIVE_NO_DATA if nothing arrived,
    /// or a negative value on error (socket failure, malformed or truncated frame).
    virtual int Receive(dmq::ByteReader& is, DmqHeader& header) = 0;
};

} // namespace dmq::transport


#endif