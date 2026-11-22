#ifndef SYSTEM_MESSAGES_H
#define SYSTEM_MESSAGES_H

/// @file SystemMessages.h
/// Message types shared between the C++ server and the Python/C# interop clients.
/// Serialized using MessagePack (MSGPACK_DEFINE field order must match the
/// interop clients' decode order exactly).

#include <msgpack.hpp>
#include <vector>

/// Actuator state published by the server.
struct Actuator {
    int   id       = 0;
    float position = 0.0f;
    float voltage  = 0.0f;
    MSGPACK_DEFINE(id, position, voltage);
};

/// Sensor reading published by the server.
struct Sensor {
    int   id       = 0;
    float supplyV  = 0.0f;
    float readingV = 0.0f;
    MSGPACK_DEFINE(id, supplyV, readingV);
};

/// Periodic data message published by the server to all subscribers.
struct DataMsg {
    std::vector<Actuator> actuators;
    std::vector<Sensor>   sensors;
    MSGPACK_DEFINE(actuators, sensors);
};

/// Command message sent by a client to adjust the server's polling rate.
struct CommandMsg {
    int pollingRateMs = 1000;
    MSGPACK_DEFINE(pollingRateMs);
};

#endif // SYSTEM_MESSAGES_H
