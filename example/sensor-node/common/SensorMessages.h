#ifndef SENSOR_MESSAGES_H
#define SENSOR_MESSAGES_H

/// @file SensorMessages.h
/// @brief Messages exchanged between the sensor node and the PC client, with
/// DelegateMQ's built-in serializer. Plain fixed-size fields only, so the
/// encoding is the same on the ESP32-S3 and the PC.

#include "port/serialize/serialize/msg_serialize.h"
#include <cstdint>

/// Statistics over one 100 ms window of the fast signal (node -> PC)
struct SignalStats : public serialize::I {
    uint32_t seq = 0;
    uint32_t rateHz = 0;        // sample rate the window was taken at
    uint32_t count = 0;         // samples in the window (rateHz / 10)
    float min = 0.0f;
    float max = 0.0f;
    float mean = 0.0f;

    std::ostream& write(::serialize& ms, std::ostream& os) override {
        ms.write(os, seq); ms.write(os, rateHz); ms.write(os, count);
        ms.write(os, min); ms.write(os, max);
        return ms.write(os, mean);
    }
    std::istream& read(::serialize& ms, std::istream& is) override {
        ms.read(is, seq); ms.read(is, rateHz); ms.read(is, count);
        ms.read(is, min); ms.read(is, max);
        return ms.read(is, mean);
    }
};

/// Temperature reading, once a second (node -> PC)
struct Temperature : public serialize::I {
    uint32_t seq = 0;
    float celsius = 0.0f;

    std::ostream& write(::serialize& ms, std::ostream& os) override {
        ms.write(os, seq);
        return ms.write(os, celsius);
    }
    std::istream& read(::serialize& ms, std::istream& is) override {
        ms.read(is, seq);
        return ms.read(is, celsius);
    }
};

/// A touch pad changed state (node -> PC)
struct Touch : public serialize::I {
    uint32_t seq = 0;
    uint8_t pad = 0;
    uint8_t touched = 0;        // 1 = touched, 0 = released

    std::ostream& write(::serialize& ms, std::ostream& os) override {
        ms.write(os, seq); ms.write(os, pad);
        return ms.write(os, touched);
    }
    std::istream& read(::serialize& ms, std::istream& is) override {
        ms.read(is, seq); ms.read(is, pad);
        return ms.read(is, touched);
    }
};

/// The node's state: once a second, and right after each command (node -> PC)
struct NodeStatus : public serialize::I {
    uint32_t seq = 0;
    uint32_t uptimeMs = 0;
    uint32_t rateHz = 0;
    uint8_t ledR = 0, ledG = 0, ledB = 0;
    uint32_t commands = 0;      // commands applied so far
    uint32_t netDropped = 0;    // messages the network thread dropped (queue full)
    float crossCoreUs = 0.0f;   // mean async-wait round trip, ControlThread -> NetThread
    float sameCoreUs = 0.0f;    // mean async-wait round trip, ControlThread -> SampleThread

    std::ostream& write(::serialize& ms, std::ostream& os) override {
        ms.write(os, seq); ms.write(os, uptimeMs); ms.write(os, rateHz);
        ms.write(os, ledR); ms.write(os, ledG); ms.write(os, ledB);
        ms.write(os, commands); ms.write(os, netDropped);
        ms.write(os, crossCoreUs);
        return ms.write(os, sameCoreUs);
    }
    std::istream& read(::serialize& ms, std::istream& is) override {
        ms.read(is, seq); ms.read(is, uptimeMs); ms.read(is, rateHz);
        ms.read(is, ledR); ms.read(is, ledG); ms.read(is, ledB);
        ms.read(is, commands); ms.read(is, netDropped);
        ms.read(is, crossCoreUs);
        return ms.read(is, sameCoreUs);
    }
};

/// Set the sample rate (PC -> node): 100 to 10000 Hz, a multiple of 100
struct SetRate : public serialize::I {
    uint32_t rateHz = 0;

    std::ostream& write(::serialize& ms, std::ostream& os) override { return ms.write(os, rateHz); }
    std::istream& read(::serialize& ms, std::istream& is) override { return ms.read(is, rateHz); }
};

/// Set the LED colour (PC -> node)
struct SetLed : public serialize::I {
    uint8_t r = 0, g = 0, b = 0;

    std::ostream& write(::serialize& ms, std::ostream& os) override {
        ms.write(os, r); ms.write(os, g);
        return ms.write(os, b);
    }
    std::istream& read(::serialize& ms, std::istream& is) override {
        ms.read(is, r); ms.read(is, g);
        return ms.read(is, b);
    }
};

#endif // SENSOR_MESSAGES_H
