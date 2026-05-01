#ifndef HEARTBEAT_MSG_H
#define HEARTBEAT_MSG_H

#include "MessageBase.h"

namespace cellutron {

/// @brief Simple heartbeat message.
struct HeartbeatMsg : public MessageBase
{
    uint32_t counter = 0;

    HeartbeatMsg() = default;
    HeartbeatMsg(uint32_t c) : counter(c) {}

    virtual std::istream& read(serialize& ms, std::istream& is) override {
        MessageBase::read(ms, is);
        return ms.read(is, counter);
    }

    virtual std::ostream& write(serialize& ms, std::ostream& os) override {
        MessageBase::write(ms, os);
        return ms.write(os, counter);
    }
};

} // namespace cellutron

#endif
