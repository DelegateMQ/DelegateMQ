#ifndef SYSTEM_MESSAGES_H
#define SYSTEM_MESSAGES_H

#include "port/serialize/serialize/msg_serialize.h"
#include <cstdint>

// Temperature reading published by the sensor node
struct TempMsg : public serialize::I {
    float celsius = 0.0f;
    uint32_t seq = 0;

    virtual std::ostream& write(::serialize& ms, std::ostream& os) override {
        ms.write(os, celsius);
        return ms.write(os, seq);
    }
    virtual std::istream& read(::serialize& ms, std::istream& is) override {
        ms.read(is, celsius);
        return ms.read(is, seq);
    }
};

// Setpoint command published by the monitor node
struct SetpointMsg : public serialize::I {
    float celsius = 0.0f;

    virtual std::ostream& write(::serialize& ms, std::ostream& os) override {
        return ms.write(os, celsius);
    }
    virtual std::istream& read(::serialize& ms, std::istream& is) override {
        return ms.read(is, celsius);
    }
};

#endif // SYSTEM_MESSAGES_H
