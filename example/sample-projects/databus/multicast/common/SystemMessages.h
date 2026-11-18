#ifndef SYSTEM_MESSAGES_H
#define SYSTEM_MESSAGES_H

#include "port/serialize/serialize/msg_serialize.h"
#include <vector>

// Simulated Actuator data
struct Actuator : public serialize::I {
    int id = 0;
    float position = 0.0f;
    float voltage = 0.0f;

    virtual std::ostream& write(::serialize& ms, std::ostream& os) override {
        ms.write(os, id);
        ms.write(os, position);
        return ms.write(os, voltage);
    }
    virtual std::istream& read(::serialize& ms, std::istream& is) override {
        ms.read(is, id);
        ms.read(is, position);
        return ms.read(is, voltage);
    }
};

// Simulated Sensor data
struct Sensor : public serialize::I {
    int id = 0;
    float supplyV = 0.0f;
    float readingV = 0.0f;

    virtual std::ostream& write(::serialize& ms, std::ostream& os) override {
        ms.write(os, id);
        ms.write(os, supplyV);
        return ms.write(os, readingV);
    }
    virtual std::istream& read(::serialize& ms, std::istream& is) override {
        ms.read(is, id);
        ms.read(is, supplyV);
        return ms.read(is, readingV);
    }
};

// Main Data Message
struct DataMsg : public serialize::I {
    std::vector<Actuator> actuators;
    std::vector<Sensor> sensors;

    virtual std::ostream& write(::serialize& ms, std::ostream& os) override {
        ms.write(os, actuators);
        return ms.write(os, sensors);
    }
    virtual std::istream& read(::serialize& ms, std::istream& is) override {
        ms.read(is, actuators);
        return ms.read(is, sensors);
    }
};

// Command Message to adjust polling rate
struct CommandMsg : public serialize::I {
    int pollingRateMs = 1000;

    virtual std::ostream& write(::serialize& ms, std::ostream& os) override {
        return ms.write(os, pollingRateMs);
    }
    virtual std::istream& read(::serialize& ms, std::istream& is) override {
        return ms.read(is, pollingRateMs);
    }
};

#endif // SYSTEM_MESSAGES_H
