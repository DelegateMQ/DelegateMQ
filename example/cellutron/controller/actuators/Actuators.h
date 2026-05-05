#ifndef _ACTUATORS_H
#define _ACTUATORS_H

#include "DelegateMQ.h"
#include "Centrifuge.h"
#include "Valve.h"
#include "Pump.h"
#include <map>

namespace cellutron {
namespace actuators {

/// @brief Singleton class for controlling hardware actuators.
class Actuators {
public:
    /// Signals for actuator status changes.
    dmq::Signal<void(int id, bool open)> OnValveChanged;
    dmq::Signal<void(int id, int speed)> OnPumpChanged;

    static Actuators& GetInstance() {
        static Actuators instance;
        return instance;
    }

    void Initialize();
    void Shutdown();

    /// Set a valve state (Asynchronous non-blocking call)
    void SetValve(int id, bool open);

    /// Set a pump speed (Asynchronous non-blocking call)
    void SetPump(int id, int speed);

    /// Get the centrifuge actuator.
    Centrifuge& GetCentrifuge() { return m_centrifuge; }

    /// Get a valve by ID.
    Valve& GetValve(int id) { return m_valves.at(id); }

    /// Get a pump by ID.
    Pump& GetPump(int id) { return m_pumps.at(id); }

    /// Get the actuator thread.
    dmq::os::Thread& GetThread() { return m_thread; }

private:
    Actuators() = default;
    ~Actuators();

    Actuators(const Actuators&) = delete;
    Actuators& operator=(const Actuators&) = delete;

    void InternalSetValve(int id, bool open);
    void InternalSetPump(int id, int speed);

    void HandleValveChanged(int id, bool open);
    void HandlePumpChanged(int id, int speed);

    // Use standardized thread name for Active Object subsystem
    dmq::os::Thread m_thread{"ActuatorsThread", 50, dmq::os::FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "Controller"};
    Centrifuge m_centrifuge;

    std::map<int, Valve> m_valves;
    std::map<int, Pump>  m_pumps;

    std::map<int, dmq::ScopedConnection> m_valveConns;
    std::map<int, dmq::ScopedConnection> m_pumpConns;
};

} // namespace actuators
} // namespace cellutron

#endif
