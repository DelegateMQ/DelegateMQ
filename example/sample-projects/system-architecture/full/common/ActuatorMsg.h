#ifndef ACTUATOR_MSG_H
#define ACTUATOR_MSG_H

#include <msgpack.hpp>

class ActuatorMsg
{
public:
    int actuatorId = 0;
    bool actuatorPosition = false;

    MSGPACK_DEFINE(actuatorId, actuatorPosition);
};

#endif