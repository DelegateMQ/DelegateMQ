#ifndef _SYSTEM_H
#define _SYSTEM_H

#include "DelegateMQ.h"
#include "util/Heartbeat.h"
#include "util/MessageGuard.h"
#include <memory>

namespace cellutron {

/// @brief Top-level system coordinator for the Controller node.
/// 
/// Handles initialization of all subsystems (Process, Actuators, Sensors),
/// network configuration, and system-wide monitoring (Heartbeat/Watchdog).
class System {
public:
    static System& GetInstance() {
        static System instance;
        return instance;
    }

    /// Initialize the entire controller system.
    void Initialize();

    /// Process periodic system maintenance.
    /// @param ms Milliseconds since last tick.
    void Tick(uint32_t ms);

    /// Get the system thread.
    dmq::os::Thread& GetThread() { return m_thread; }

private:
    System() : m_heartbeat("Controller", topics::CONTROLLER_HEARTBEAT, m_thread) {}
    ~System() = default;

    System(const System&) = delete;
    System& operator=(const System&) = delete;

    void SetupLocalSubscriptions();
    void SetupNetwork();
    void SetupWatchdog();

    dmq::os::Thread m_thread{"SystemThread", 200, dmq::os::FullPolicy::TIMEOUT};

    // Connections to the local DataBus
    dmq::ScopedConnection m_startConn;
    dmq::ScopedConnection m_stopConn;
    dmq::ScopedConnection m_faultConn;

    // Guards for incoming commands
    MessageGuard m_startGuard;
    MessageGuard m_stopGuard;

    // Heartbeat component
    util::Heartbeat m_heartbeat;
};

} // namespace cellutron

#endif // _SYSTEM_H
