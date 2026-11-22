#ifndef DATA_MSG_H
#define DATA_MSG_H

#include "Actuator.h"
#include "Sensor.h"
#include "DelegateMQ.h"
#include <vector>

class DataMsg : public serialize::I
{
public:
    std::vector<ActuatorState> actuators;
    std::vector<SensorData> sensors;

    virtual ~DataMsg() = default;

    virtual dmq::ByteWriter& write(serialize& ms, dmq::ByteWriter& os) override
    {
        ms.write(os, actuators);
        ms.write(os, sensors);
        return os;
    }

    virtual dmq::ByteReader& read(serialize& ms, dmq::ByteReader& is) override
    {
        ms.read(is, actuators);
        ms.read(is, sensors);
        return is;
    }
};

#endif