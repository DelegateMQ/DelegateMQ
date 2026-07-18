#ifndef _VALVE_H
#define _VALVE_H

#include "DelegateMQ.h"
#include "Logger.h"
#include "messages/ActuatorStatusMsg.h"
#include "util/Constants.h"
#include <cstdio>

namespace cellutron {
namespace actuators {

/// @brief Represents a single on/off solenoid valve.
///
/// SetState() must be called on the ActuatorThread (enforced by the
/// Actuators manager via a blocking delegate). The current position is
/// published to the DataBus on every state change so the logging
/// subsystem and any other subscribers receive it automatically.
class Valve
{
public:
    /// Signal emitted when valve state changes.
    /// int: The valve ID.
    /// bool: true if OPEN, false if CLOSED.
    dmq::Signal<void(int, bool)> OnStateChanged;

    explicit Valve(uint8_t id) : m_id(id) {}

    /// Open or close the valve. Publishes the new state to the DataBus.
    /// Runs on the ActuatorThread.
    void SetState(bool open)
    {
        m_isOpen = open;
        printf("[Valve %d] -> %s\n", m_id, open ? "OPEN" : "CLOSED");
        dmq::databus::DataBus::Publish<ActuatorStatusMsg>(
            topics::STATUS_ACTUATOR,
            { ActuatorType::VALVE, m_id, open ? uint8_t(1) : uint8_t(0) });
        OnStateChanged(m_id, open);
    }

    bool    IsOpen() const { return m_isOpen; }
    uint8_t GetId()  const { return m_id; }

private:
    uint8_t m_id;
    bool    m_isOpen = false;
};

} // namespace actuators
} // namespace cellutron

#endif // _VALVE_H
