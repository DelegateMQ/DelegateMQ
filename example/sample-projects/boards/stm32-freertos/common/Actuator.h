#ifndef ACTUATOR_H
#define ACTUATOR_H

#include "DelegateMQ.h"

class ActuatorState : public serialize::I
{
public:
    ActuatorState() = default;
    ActuatorState(int _id) : id(_id) {}

    int16_t id = 0;
    bool position = false;
    float voltage = 0.0f;

    virtual std::ostream& write(serialize& ms, std::ostream& os) override
    {
        ms.write(os, id);
        ms.write(os, position);
        ms.write(os, voltage);
        return os;
    }

    virtual std::istream& read(serialize& ms, std::istream& is) override
    {
        ms.read(is, id);
        ms.read(is, position);
        ms.read(is, voltage);
        return is;
    }
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