#ifndef CENTRIFUGE_STATUS_MSG_H
#define CENTRIFUGE_STATUS_MSG_H

#include "MessageBase.h"

namespace cellutron {

/// @brief Message containing the current centrifuge status.
/// @details IO CPU broadcasts this every 10ms.
struct CentrifugeStatusMsg : public MessageBase
{
    uint16_t rpm = 0;

    CentrifugeStatusMsg() = default;
    CentrifugeStatusMsg(uint16_t val) : rpm(val) {}

    virtual std::istream& read(serialize& ms, std::istream& is) override {
        MessageBase::read(ms, is);
        return ms.read(is, rpm);
    }

    virtual std::ostream& write(serialize& ms, std::ostream& os) override {
        MessageBase::write(ms, os);
        return ms.write(os, rpm);
    }
};

} // namespace cellutron

#endif
