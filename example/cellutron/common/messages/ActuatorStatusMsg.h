#ifndef ACTUATOR_STATUS_MSG_H
#define ACTUATOR_STATUS_MSG_H

#include "MessageBase.h"

namespace cellutron {

enum class ActuatorType { VALVE, PUMP };

struct ActuatorStatusMsg : public MessageBase
{
    ActuatorType type = ActuatorType::VALVE;
    uint8_t id = 0;
    int16_t value = 0; // 0/1 for valve, -100 to 100 for pump

    ActuatorStatusMsg() = default;
    ActuatorStatusMsg(ActuatorType t, uint8_t i, int16_t v) : type(t), id(i), value(v) {}

    virtual dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override {
        MessageBase::read(ms, is);
        uint8_t t;
        ms.read(is, t);
        type = static_cast<ActuatorType>(t);
        ms.read(is, id);
        ms.read(is, value);
        return is;
    }

    virtual dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override {
        MessageBase::write(ms, os);
        uint8_t t = static_cast<uint8_t>(type);
        ms.write(os, t);
        ms.write(os, id);
        ms.write(os, value);
        return os;
    }
};

} // namespace cellutron

#endif
