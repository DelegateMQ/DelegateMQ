#ifndef PUMPTRON_PUMP_STATUS_MSG_H
#define PUMPTRON_PUMP_STATUS_MSG_H

#include "MessageBase.h"
#include "AlarmMsg.h"

namespace pumptron {

/// @brief Pump controller state machine states.
enum class PumpState : uint8_t { IDLE, PRIMING, RUNNING, STOPPING, FAULT };

/// @brief Pump state (controller -> GUI). Sent RELIABLE on every state change
///        and in response to PumpCommand::QUERY.
struct PumpStatusMsg : public MessageBase
{
    PumpState state = PumpState::IDLE;
    AlarmCode faultCode = AlarmCode::NONE;   ///< Latched fault while state == FAULT
    uint16_t setpointRpm = 0;

    PumpStatusMsg() = default;
    PumpStatusMsg(PumpState s, AlarmCode f, uint16_t sp) : state(s), faultCode(f), setpointRpm(sp) {}

    virtual std::istream& read(serialize& ms, std::istream& is) override {
        MessageBase::read(ms, is);
        uint8_t s = 0, f = 0;
        ms.read(is, s);
        ms.read(is, f);
        state = static_cast<PumpState>(s);
        faultCode = static_cast<AlarmCode>(f);
        return ms.read(is, setpointRpm);
    }

    virtual std::ostream& write(serialize& ms, std::ostream& os) override {
        MessageBase::write(ms, os);
        uint8_t s = static_cast<uint8_t>(state);
        uint8_t f = static_cast<uint8_t>(faultCode);
        ms.write(os, s);
        ms.write(os, f);
        return ms.write(os, setpointRpm);
    }
};

inline const char* ToString(PumpState s) {
    switch (s) {
        case PumpState::IDLE:     return "IDLE";
        case PumpState::PRIMING:  return "PRIMING";
        case PumpState::RUNNING:  return "RUNNING";
        case PumpState::STOPPING: return "STOPPING";
        case PumpState::FAULT:    return "FAULT";
    }
    return "UNKNOWN";
}

} // namespace pumptron

#endif
