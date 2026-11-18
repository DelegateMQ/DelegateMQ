#ifndef DATA_MSG_H
#define DATA_MSG_H

#include "Actuator.h"
#include "Sensor.h"
#include <msgpack.hpp>
#include <vector>

class DataMsg
{
public:
    std::vector<ActuatorState> actuators;
    std::vector<SensorData> sensors;

    MSGPACK_DEFINE(actuators, sensors);
};

#endif