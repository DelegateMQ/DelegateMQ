#ifndef SENSOR_TOPICS_H
#define SENSOR_TOPICS_H

/// @file SensorTopics.h
/// @brief DataBus topics and network ports shared by the sensor node and the PC
/// client. The remote IDs must match on both sides.

#include "DelegateMQ.h"
#include <cstdint>

namespace SensorTopic {
    // Node -> PC
    inline constexpr const char* const SignalStats = "sensor/signal/stats";
    inline constexpr const char* const Temperature = "sensor/temp";
    inline constexpr const char* const Touch       = "sensor/touch";
    inline constexpr const char* const Status      = "node/status";

    // PC -> node
    inline constexpr const char* const SetRate     = "cmd/rate";
    inline constexpr const char* const SetLed      = "cmd/led";

    enum RemoteId : dmq::DelegateRemoteId {
        SignalStatsId = 200,
        TemperatureId = 201,
        TouchId       = 202,
        StatusId      = 203,
        SetRateId     = 210,
        SetLedId      = 211
    };
}

namespace SensorNet {
    /// The node sends to the PC on DATA_PORT; the PC sends commands to the node on CMD_PORT
    inline constexpr uint16_t DATA_PORT = 9100;
    inline constexpr uint16_t CMD_PORT  = 9101;
}

#endif // SENSOR_TOPICS_H
