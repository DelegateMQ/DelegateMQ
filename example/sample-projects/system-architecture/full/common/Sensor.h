#ifndef SENSOR_H
#define SENSOR_H

#include <msgpack.hpp>

class SensorData
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

    MSGPACK_DEFINE(id, supplyV, readingV);
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