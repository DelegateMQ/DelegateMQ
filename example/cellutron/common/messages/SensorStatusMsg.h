#ifndef SENSOR_STATUS_MSG_H
#define SENSOR_STATUS_MSG_H

#include "MessageBase.h"

namespace cellutron {

enum class SensorType { PRESSURE, AIR_IN_LINE };

struct SensorStatusMsg : public MessageBase
{
    SensorType type = SensorType::PRESSURE;
    int16_t value = 0;

    SensorStatusMsg() = default;
    SensorStatusMsg(SensorType t, int16_t v) : type(t), value(v) {}

    virtual std::istream& read(serialize& ms, std::istream& is) override {
        MessageBase::read(ms, is);
        uint8_t t;
        ms.read(is, t);
        type = static_cast<SensorType>(t);
        ms.read(is, value);
        return is;
    }

    virtual std::ostream& write(serialize& ms, std::ostream& os) override {
        MessageBase::write(ms, os);
        uint8_t t = static_cast<uint8_t>(type);
        ms.write(os, t);
        ms.write(os, value);
        return os;
    }
};

} // namespace cellutron

#endif
