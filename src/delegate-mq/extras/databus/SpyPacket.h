#ifndef DMQ_SPY_PACKET_H
#define DMQ_SPY_PACKET_H

#include "delegate/DelegateOpt.h"
#include "port/serialize/serialize/msg_serialize.h"

namespace dmq::databus {

/// @brief Standardized packet containing bus traffic metadata.
/// @details This struct is passed to DataBus::Monitor subscribers and is 
/// designed to be serialized for transmission to external diagnostic tools.
/// 
/// The timestamp_us field uses dmq::Clock::now(), which typically provides 
/// monotonic time since boot.
struct SpyPacket : public ::serialize::I {
    XALLOCATOR
    SpyPacket() = default;
    virtual ~SpyPacket() = default;
    SpyPacket(const dmq::xstring& t, const dmq::xstring& v, uint64_t ts, const dmq::xstring& id = "")
        : topic(t), value(v), timestamp_us(ts), nodeId(id) {}

    dmq::xstring topic;      ///< The name of the data topic.
    dmq::xstring value;      ///< Stringified representation of the data (or "?" if no stringifier registered).
    uint64_t timestamp_us = 0;  ///< Microseconds (usually since boot) when the message appeared on
                                ///< the reporting node's DataBus.
    dmq::xstring nodeId;    ///< The node that reported the message: the one whose DataBus it
                            ///< appeared on, published there or received from a remote
                            ///< participant. Not necessarily where the message originated.

    dmq::ByteWriter& write(::serialize& ms, dmq::ByteWriter& os) override {
        ms.write(os, topic);
        ms.write(os, value);
        ms.write(os, timestamp_us);
        return ms.write(os, nodeId);
    }

    dmq::ByteReader& read(::serialize& ms, dmq::ByteReader& is) override {
        ms.read(is, topic);
        ms.read(is, value);
        ms.read(is, timestamp_us);
        return ms.read(is, nodeId);
    }
};
} // namespace dmq::databus


#endif // DMQ_SPY_PACKET_H
