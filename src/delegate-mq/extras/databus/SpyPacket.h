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
    SpyPacket() = default;
    SpyPacket(const dmq::xstring& t, const dmq::xstring& v, uint64_t ts) 
        : topic(t), value(v), timestamp_us(ts) {}

    dmq::xstring topic;      ///< The name of the data topic.
    dmq::xstring value;      ///< Stringified representation of the data (or "?" if no stringifier registered).
    uint64_t timestamp_us;  ///< Microseconds (usually since boot) when the message was published.

    std::ostream& write(::serialize& ms, std::ostream& os) override {
        ms.write(os, topic);
        ms.write(os, value);
        return ms.write(os, timestamp_us);
    }

    std::istream& read(::serialize& ms, std::istream& is) override {
        ms.read(is, topic);
        ms.read(is, value);
        return ms.read(is, timestamp_us);
    }
};

} // namespace dmq::databus


#endif // DMQ_SPY_PACKET_H
