// DmqFrame.h
// @see https://github.com/DelegateMQ/DelegateMQ
// Unframing for the diagnostic consoles: every reporter (SpyReporter,
// NodeReporter, and the desktop SpyBridge/NodeBridge built on them) sends each
// packet as one DelegateMQ message.

#ifndef DMQ_FRAME_H
#define DMQ_FRAME_H

#include "DelegateMQ.h"
#include <cstddef>
#include <cstdint>

/// @brief Split a received datagram into its message header and payload.
/// @param[out] id The header's remote ID (SPY_REMOTE_ID, NODE_INFO_REMOTE_ID, ...).
/// @param[out] payload A view of the serialized packet, inside data.
/// @return false if the datagram isn't one valid DelegateMQ message (too short,
///         wrong marker or version, or a length past the datagram's end).
inline bool UnframeDmq(const uint8_t* data, size_t size, uint16_t& id, dmq::ByteReader& payload) {
    using dmq::transport::DmqHeader;
    if (size < DmqHeader::HEADER_SIZE)
        return false;
    DmqHeader header;
    header.Decode(data);
    if (!header.IsValid() || header.GetLength() > size - DmqHeader::HEADER_SIZE)
        return false;
    id = header.GetId();
    payload.assign(reinterpret_cast<const char*>(data + DmqHeader::HEADER_SIZE), header.GetLength());
    return true;
}

#endif // DMQ_FRAME_H
