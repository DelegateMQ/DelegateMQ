#ifndef _ALARMS_H
#define _ALARMS_H

#include "DelegateMQ.h"
#include "messages/HeartbeatMsg.h"
#include "messages/FaultMsg.h"
#include "util/MessageGuard.h"
#include "extras/databus/DeadlineSubscription.h"
#include <string>
#include <memory>

namespace cellutron {
namespace util {

/// @brief Singleton class for centralized alarm handling in the GUI.
class Alarms {
public:
    /// Signal emitted when an alarm state changes.
    /// std::string: The human-readable alarm message.
    /// bool: True if this is an active alarm (Red), False for "No Alarm" (Green).
    dmq::Signal<void(std::string, bool)> OnAlarmChanged;

    static Alarms& GetInstance() {
        static Alarms instance;
        return instance;
    }

    /// Initialize the Alarm subsystem.
    void Initialize();

    /// Shutdown the Alarm subsystem.
    void Shutdown();

    /// Reset the current alarm state.
    void Reset();

private:
    Alarms() = default;
    ~Alarms() { Shutdown(); }

    void SetAlarm(const std::string& message, bool active);

    // Standardized thread name for Active Object subsystem
    dmq::os::Thread m_thread{"AlarmsThread", 200, dmq::os::FullPolicy::DROP};

    dmq::ScopedConnection m_faultConn;
    dmq::ScopedConnection m_runStatusConn;
    std::unique_ptr<dmq::util::Timer> m_alarmGraceTimer;
    dmq::ScopedConnection m_alarmGraceConn;
    std::unique_ptr<dmq::databus::DeadlineSubscription<HeartbeatMsg>> m_safetyWatchdog;
    std::unique_ptr<dmq::databus::DeadlineSubscription<HeartbeatMsg>> m_controllerWatchdog;

    MessageGuard m_statusGuard;

    std::string m_currentMessage = "No Alarm";
    bool m_alarmActive = false;
    uint32_t m_ticksWaited = 0;
};

} // namespace util
} // namespace cellutron

#endif
