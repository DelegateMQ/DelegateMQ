#ifndef MSG_H
#define MSG_H

/// @file Msg.h
/// @brief Shared message types, topic names, and remote IDs for the
///        databus-zephyr sample.
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.

#include "DelegateMQ.h"
#include "port/serialize/serialize/msg_serialize.h"

// ---------------------------------------------------------------------------
// Message types
// ---------------------------------------------------------------------------

/// Sensor reading published by the Zephyr server every N ms.
struct SensorMsg : public serialize::I
{
    int   sensorId = 1;
    float temp     = 0.0f;

    std::ostream& write(::serialize& ms, std::ostream& os) override
    {
        ms.write(os, sensorId);
        return ms.write(os, temp);
    }
    std::istream& read(::serialize& ms, std::istream& is) override
    {
        ms.read(is, sensorId);
        return ms.read(is, temp);
    }
};

/// Command published by the Linux/Windows client to adjust the server's
/// publish interval.
struct CmdMsg : public serialize::I
{
    int intervalMs = 1000;

    std::ostream& write(::serialize& ms, std::ostream& os) override
    {
        return ms.write(os, intervalMs);
    }
    std::istream& read(::serialize& ms, std::istream& is) override
    {
        return ms.read(is, intervalMs);
    }
};

/// Alarm published by the Zephyr server on the same transport as SensorMsg.
/// Demonstrates multiple message types sharing one UDP socket (port 9000),
/// demultiplexed by DelegateRemoteId in the DmqHeader.
struct AlarmMsg : public serialize::I
{
    int  alarmId = 1;
    bool active  = false;

    std::ostream& write(::serialize& ms, std::ostream& os) override
    {
        ms.write(os, alarmId);
        return ms.write(os, active);
    }
    std::istream& read(::serialize& ms, std::istream& is) override
    {
        ms.read(is, alarmId);
        return ms.read(is, active);
    }
};

// ---------------------------------------------------------------------------
// Topic names and remote IDs
// ---------------------------------------------------------------------------

namespace topics
{
    inline constexpr const char* SensorTemp  = "sensor/temp";
    inline constexpr const char* CmdRate     = "cmd/rate";
    inline constexpr const char* AlarmStatus = "alarm/status";

    enum RemoteId : dmq::DelegateRemoteId
    {
        SensorTempId  = 1,
        CmdRateId     = 2,
        AlarmStatusId = 3
    };
}

#endif // MSG_H
