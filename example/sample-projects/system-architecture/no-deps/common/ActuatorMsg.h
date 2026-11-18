#ifndef ACTUATOR_MSG_H
#define ACTUATOR_MSG_H

#include "DelegateMQ.h"

class ActuatorMsg : public serialize::I
{
public:
    int actuatorId = 0;
    bool actuatorPosition = false;

    virtual std::ostream& write(serialize& ms, std::ostream& os) override
    {
        ms.write(os, actuatorId);
        ms.write(os, actuatorPosition);
        return os;
    }

    virtual std::istream& read(serialize& ms, std::istream& is) override
    {
        ms.read(is, actuatorId);
        ms.read(is, actuatorPosition);
        return is;
    }
};

#endif