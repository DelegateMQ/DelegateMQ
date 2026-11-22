#ifndef ACTUATOR_MSG_H
#define ACTUATOR_MSG_H

#include "DelegateMQ.h"

class ActuatorMsg : public serialize::I
{
public:
    int16_t actuatorId = 0;
    bool actuatorPosition = false;

    virtual ~ActuatorMsg() = default;

    virtual dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override
    {
        ms.write(os, actuatorId);
        ms.write(os, actuatorPosition);
        return os;
    }

    virtual dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override
    {
        ms.read(is, actuatorId);
        ms.read(is, actuatorPosition);
        return is;
    }
};

#endif