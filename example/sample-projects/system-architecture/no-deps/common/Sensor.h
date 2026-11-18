#ifndef SENSOR_H
#define SENSOR_H

#include "DelegateMQ.h"

class SensorData : public serialize::I
{
public:
    SensorData() = default;
    SensorData(int _id) : id(_id) {}
    SensorData& operator=(const SensorData& other)
    {
        id = other.id;
        supplyV = other.supplyV;
        readingV = other.readingV;
        return *this;
    }

    int id = 0;
    float supplyV = 0.0f;
    float readingV = 0.0f;

    virtual std::ostream& write(serialize& ms, std::ostream& os) override
    {
        ms.write(os, id);
        ms.write(os, supplyV);
        ms.write(os, readingV);
        return os;
    }

    virtual std::istream& read(serialize& ms, std::istream& is) override
    {
        ms.read(is, id);
        ms.read(is, supplyV);
        ms.read(is, readingV);
        return is;
    }
};

class Sensor
{
public:
    Sensor(int id) : m_data(id) {}

    SensorData& GetSensorData() 
    { 
        // Simulate data changing
        m_data.readingV += 0.1f;
        m_data.supplyV += 0.1f;
        return m_data; 
    }

private:
    SensorData m_data;
};

#endif