#ifndef CENTRIFUGE_SPEED_MSG_H
#define CENTRIFUGE_SPEED_MSG_H

#include "MessageBase.h"

namespace cellutron {

/// @brief Message to command centrifuge speed.
/// @details IO CPU accepts this message and applies the RPM directly.
struct CentrifugeSpeedMsg : public MessageBase
{
    uint16_t rpm = 0;

    CentrifugeSpeedMsg() = default;
    CentrifugeSpeedMsg(uint16_t val) : rpm(val) {}

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
