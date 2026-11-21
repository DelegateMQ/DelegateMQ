#ifndef SENSOR_TOPICS_H
#define SENSOR_TOPICS_H

/// @file SensorTopics.h
/// @brief DataBus topics and network ports shared by the sensor node and the PC
/// client. The remote IDs must match on both sides.

#include "DelegateMQ.h"
#include <chrono>
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
    /// The PC client sends Start every second (a lease); the node publishes only
    /// while they keep coming, so an idle node sends nothing
    inline constexpr const char* const Start       = "cmd/start";

    enum RemoteId : dmq::DelegateRemoteId {
        SignalStatsId = 200,
        TemperatureId = 201,
        TouchId       = 202,
        StatusId      = 203,
        SetRateId     = 210,
        SetLedId      = 211,
        StartId       = 212
    };
}

namespace SensorNet {
    /// The node sends to the PC on DATA_PORT; the PC sends commands to the node on CMD_PORT
    inline constexpr uint16_t DATA_PORT = 9100;
    inline constexpr uint16_t CMD_PORT  = 9101;

    /// Commands are reliable: the client re-sends one not ACKed within
    /// CMD_ACK_TIMEOUT, up to CMD_RETRIES times
    inline constexpr auto CMD_ACK_TIMEOUT = std::chrono::milliseconds(250);
    inline constexpr int CMD_RETRIES = 3;

    /// The client's whole retry window. The node's command Participant drops a
    /// transport sequence number seen again within it as a retransmission
    /// (Participant::SetSeqHistoryExpiry()); a restarted client, which numbers
    /// from 1 again, is accepted once it has passed.
    inline constexpr auto CMD_RETRY_WINDOW = CMD_ACK_TIMEOUT * (CMD_RETRIES + 1);
}

#endif // SENSOR_TOPICS_H
