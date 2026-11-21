#ifndef SYSTEM_MESSAGES_H
#define SYSTEM_MESSAGES_H

#include "port/serialize/serialize/msg_serialize.h"
#include <cstdint>

// Temperature reading published by the sensor node
struct TempMsg : public serialize::I {
    float celsius = 0.0f;
    uint32_t seq = 0;

    virtual dmq::ByteWriter& write(::serialize& ms, dmq::ByteWriter& os) override {
        ms.write(os, celsius);
        return ms.write(os, seq);
    }
    virtual dmq::ByteReader& read(::serialize& ms, dmq::ByteReader& is) override {
        ms.read(is, celsius);
        return ms.read(is, seq);
    }
};

// Setpoint command published by the monitor node
struct SetpointMsg : public serialize::I {
    float celsius = 0.0f;

    virtual dmq::ByteWriter& write(::serialize& ms, dmq::ByteWriter& os) override {
        return ms.write(os, celsius);
    }
    virtual dmq::ByteReader& read(::serialize& ms, dmq::ByteReader& is) override {
        return ms.read(is, celsius);
    }
};

#endif // SYSTEM_MESSAGES_H
