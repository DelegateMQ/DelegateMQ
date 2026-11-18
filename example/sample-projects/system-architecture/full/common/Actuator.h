#ifndef ACTUATOR_H
#define ACTUATOR_H

#include <msgpack.hpp>

class ActuatorState
{
public:
    ActuatorState() = default;
    ActuatorState(int _id) : id(_id) {}

    int id = 0;
    bool position = false;
    float voltage = 0.0f;

    MSGPACK_DEFINE(id, position, voltage);
};

class Actuator
{
public:
    Actuator(int id) : m_state(id) {}

    void SetPosition(bool position) { m_state.position = position; }

    ActuatorState& GetState() 
    { 
        // Simulate data changing
        m_state.voltage += 0.1f;
        return m_state; 
    }

private:
    ActuatorState m_state;
};

#endif