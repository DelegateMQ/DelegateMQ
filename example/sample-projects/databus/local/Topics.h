#ifndef DATABUS_LOCAL_TOPICS_H
#define DATABUS_LOCAL_TOPICS_H

// Topics.h
// Topic names, message types, and stringifiers for databus/local.
//
// No ISerializer is needed here: a local DataBus delivers objects directly
// between threads in the same process. Every message struct is kept trivially
// copyable with fixed-width fields, so if Sensor is later split into another
// process or onto a second core, these topics can cross that link with
// dmq::serialization::raw::Serializer (raw byte copies, for peers that share
// an ABI) and this header stays unchanged.

#include "DelegateMQ.h"

#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace topics {
    constexpr const char* Setpoint    = "config/setpoint";
    constexpr const char* Calibration = "config/calibration";
    constexpr const char* SensorTemp  = "sensor/temp";
    constexpr const char* ControlCmd  = "control/cmd";
    constexpr const char* Orphan      = "diagnostics/orphan";
}

struct SetpointMsg {
    float targetC = 25.0f;
    float alarmC  = 28.0f;
};

struct CalibrationMsg {
    float offsetC = 0.0f;
};

struct SensorReading {
    uint32_t seq   = 0;
    float    tempC = 0.0f;
};

enum class HeaterMode : uint8_t {
    Off          = 0,
    Heat         = 1,
    Cool         = 2,
    SafeShutdown = 3,
};

struct ControlCmd {
    uint32_t   seq       = 0;
    HeaterMode mode      = HeaterMode::Off;
    float      lastTempC = 0.0f;
};

static_assert(std::is_trivially_copyable_v<SetpointMsg>,    "POD for future raw-copy split");
static_assert(std::is_trivially_copyable_v<CalibrationMsg>, "POD for future raw-copy split");
static_assert(std::is_trivially_copyable_v<SensorReading>,  "POD for future raw-copy split");
static_assert(std::is_trivially_copyable_v<ControlCmd>,     "POD for future raw-copy split");

inline const char* HeaterModeName(HeaterMode mode) {
    switch (mode) {
        case HeaterMode::Off:          return "Off";
        case HeaterMode::Heat:         return "Heat";
        case HeaterMode::Cool:         return "Cool";
        case HeaterMode::SafeShutdown: return "SafeShutdown";
    }
    return "Unknown";
}

// Stringifiers format messages for DataBus::Monitor() and dmq-spy (SpyBridge).
inline void RegisterTopicStringifiers() {
    using dmq::databus::DataBus;

    DataBus::RegisterStringifier<SetpointMsg>(topics::Setpoint, [](const SetpointMsg& m) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "target=%.1fC alarm=%.1fC", m.targetC, m.alarmC);
        return dmq::xstring(buf);
    });

    DataBus::RegisterStringifier<CalibrationMsg>(topics::Calibration, [](const CalibrationMsg& m) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "offset=%.2fC", m.offsetC);
        return dmq::xstring(buf);
    });

    DataBus::RegisterStringifier<SensorReading>(topics::SensorTemp, [](const SensorReading& r) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "seq=%u temp=%.1fC", r.seq, r.tempC);
        return dmq::xstring(buf);
    });

    DataBus::RegisterStringifier<ControlCmd>(topics::ControlCmd, [](const ControlCmd& c) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "seq=%u mode=%s lastTemp=%.1fC",
                      c.seq, HeaterModeName(c.mode), c.lastTempC);
        return dmq::xstring(buf);
    });
}

#endif // DATABUS_LOCAL_TOPICS_H
