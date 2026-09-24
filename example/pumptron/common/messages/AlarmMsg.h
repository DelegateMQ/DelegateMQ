#ifndef PUMPTRON_ALARM_MSG_H
#define PUMPTRON_ALARM_MSG_H

#include "MessageBase.h"

namespace pumptron {

/// @brief Alarm and fault identifiers. Also used as the latched fault code in
///        PumpStatusMsg.
/// @note Values index per-alarm tables (see ALARM_CODE_COUNT): keep them
///       contiguous from 0 with no explicit values, and add new codes before
///       COUNT.
enum class AlarmCode : uint8_t {
    NONE = 0,
    TEMP_HIGH,          ///< Warning: motor temperature above TEMP_WARN_C
    VIBRATION_HIGH,     ///< Warning: vibration above VIB_WARN_G
    OVER_TEMP,          ///< Fault: motor temperature above TEMP_TRIP_C
    VIBRATION_TRIP,     ///< Fault: vibration above VIB_TRIP_G for VIB_TRIP_TIME
    ESTOP_REMOTE,       ///< Fault: E-STOP pressed on the GUI
    ESTOP_LOCAL,        ///< Fault: user button pressed on the controller board
    GUI_LINK_LOST,      ///< Warning: GUI heartbeat lost; pump safe-stopped
    LINK_DEGRADED,      ///< Warning: link/DataBus errors (delivery failures, drops, backlog)
    COUNT               ///< Number of codes -- not an alarm; keep last
};

/// Size for tables indexed by AlarmCode.
inline constexpr size_t ALARM_CODE_COUNT = static_cast<size_t>(AlarmCode::COUNT);

enum class AlarmSeverity : uint8_t { INFO, WARNING, FAULT };

/// @brief Alarm raised/cleared event (controller -> GUI). Sent RELIABLE.
struct AlarmMsg : public MessageBase
{
    AlarmCode code = AlarmCode::NONE;
    AlarmSeverity severity = AlarmSeverity::INFO;
    bool active = false;    ///< true = raised, false = cleared

    AlarmMsg() = default;
    AlarmMsg(AlarmCode c, AlarmSeverity s, bool a) : code(c), severity(s), active(a) {}

    virtual std::istream& read(serialize& ms, std::istream& is) override {
        MessageBase::read(ms, is);
        uint8_t c = 0, s = 0;
        ms.read(is, c);
        ms.read(is, s);
        code = static_cast<AlarmCode>(c);
        severity = static_cast<AlarmSeverity>(s);
        return ms.read(is, active);
    }

    virtual std::ostream& write(serialize& ms, std::ostream& os) override {
        MessageBase::write(ms, os);
        uint8_t c = static_cast<uint8_t>(code);
        uint8_t s = static_cast<uint8_t>(severity);
        ms.write(os, c);
        ms.write(os, s);
        return ms.write(os, active);
    }
};

inline const char* ToString(AlarmCode c) {
    switch (c) {
        case AlarmCode::NONE:           return "NONE";
        case AlarmCode::TEMP_HIGH:      return "MOTOR TEMP HIGH";
        case AlarmCode::VIBRATION_HIGH: return "VIBRATION HIGH";
        case AlarmCode::OVER_TEMP:      return "OVER-TEMP TRIP";
        case AlarmCode::VIBRATION_TRIP: return "VIBRATION TRIP";
        case AlarmCode::ESTOP_REMOTE:   return "E-STOP (GUI)";
        case AlarmCode::ESTOP_LOCAL:    return "E-STOP (BOARD BUTTON)";
        case AlarmCode::GUI_LINK_LOST:  return "GUI LINK LOST - SAFE STOP";
        case AlarmCode::LINK_DEGRADED:  return "LINK DEGRADED";
        case AlarmCode::COUNT:          break;
    }
    return "UNKNOWN";
}

inline const char* ToString(AlarmSeverity s) {
    switch (s) {
        case AlarmSeverity::INFO:    return "INFO";
        case AlarmSeverity::WARNING: return "WARN";
        case AlarmSeverity::FAULT:   return "FAULT";
    }
    return "?";
}

} // namespace pumptron

#endif
