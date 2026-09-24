#ifndef PUMPTRON_PUMP_COMMAND_MSG_H
#define PUMPTRON_PUMP_COMMAND_MSG_H

#include "MessageBase.h"

namespace pumptron {

/// @brief Operator commands sent from the GUI to the pump controller.
enum class PumpCommand : uint8_t {
    START,      ///< IDLE -> PRIMING -> RUNNING
    STOP,       ///< Controlled ramp down to IDLE
    ESTOP,      ///< Immediate coast to zero and latch FAULT
    RESET,      ///< Clear a latched FAULT (only once the pump is stopped and cool)
    SET_SPEED,  ///< Change the RUNNING speed setpoint (setpointRpm)
    QUERY       ///< Ask the controller to republish its current status
};

/// @brief Command message (GUI -> controller). Sent RELIABLE.
struct PumpCommandMsg : public MessageBase
{
    PumpCommand command = PumpCommand::QUERY;
    uint16_t setpointRpm = 0;

    PumpCommandMsg() = default;
    PumpCommandMsg(PumpCommand c, uint16_t rpm = 0) : command(c), setpointRpm(rpm) {}

    virtual std::istream& read(serialize& ms, std::istream& is) override {
        MessageBase::read(ms, is);
        uint8_t c = 0;
        ms.read(is, c);
        command = static_cast<PumpCommand>(c);
        return ms.read(is, setpointRpm);
    }

    virtual std::ostream& write(serialize& ms, std::ostream& os) override {
        MessageBase::write(ms, os);
        uint8_t c = static_cast<uint8_t>(command);
        ms.write(os, c);
        return ms.write(os, setpointRpm);
    }
};

} // namespace pumptron

#endif
