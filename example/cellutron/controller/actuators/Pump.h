#ifndef _PUMP_H
#define _PUMP_H

#include "DelegateMQ.h"
#include "Logger.h"
#include "messages/ActuatorStatusMsg.h"
#include "util/Constants.h"
#include <cstdint>
#include <cstdio>

namespace cellutron {
namespace actuators {

/// @brief Represents a single peristaltic pump.
class Pump {
public:
    /// Signal emitted when pump speed changes.
    /// int: The pump ID.
    /// int: The new speed (-100 to 100).
    dmq::Signal<void(int, int)> OnSpeedChanged;

    Pump(uint8_t id) : m_id(id) {}

    /// Set speed (-100 to 100%).
    void SetSpeed(int speed) {
        // Clamp speed to valid range
        if (speed > 100) m_speed = 100;
        else if (speed < -100) m_speed = -100;
        else m_speed = speed;

        printf("[Pump %d] -> %d%%\n", m_id, m_speed);
        dmq::databus::DataBus::Publish<ActuatorStatusMsg>(
            topics::STATUS_ACTUATOR,
            { ActuatorType::PUMP, m_id, static_cast<int16_t>(m_speed) });
        OnSpeedChanged(m_id, m_speed);
    }

    int GetSpeed() const { return m_speed; }
    uint8_t GetId()    const { return m_id; }

private:
    uint8_t m_id;
    int m_speed = 0;
};

} // namespace actuators
} // namespace cellutron

#endif // _PUMP_H
