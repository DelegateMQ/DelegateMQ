#ifndef PUMPTRON_HEARTBEAT_MSG_H
#define PUMPTRON_HEARTBEAT_MSG_H

#include "MessageBase.h"

namespace pumptron {

/// @brief Liveness heartbeat. Each node publishes its own topic every
///        HEARTBEAT_PERIOD; the peer watches it with a DeadlineSubscription.
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

} // namespace pumptron

#endif
