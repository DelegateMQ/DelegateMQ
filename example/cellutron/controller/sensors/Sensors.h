#ifndef _SENSORS_H
#define _SENSORS_H

#include "DelegateMQ.h"
#include "AirSensor.h"
#include "PressureSensor.h"
#include "RpmSensor.h"
#include "extras/util/Timer.h"

namespace cellutron {
namespace sensors {

/// @brief Singleton class for monitoring hardware sensors.
class Sensors {
public:
    static Sensors& GetInstance() {
        static Sensors instance;
        return instance;
    }

    void Initialize();
    void Shutdown();

    /// Get the sensors thread.
    dmq::os::Thread& GetThread() { return m_thread; }

    /// Get current pressure (Blocking call)
    int GetPressure();

    /// Check if air is detected in the line (Blocking call)
    bool IsAirInLine();

private:
    Sensors() = default;
    ~Sensors();

    Sensors(const Sensors&) = delete;
    Sensors& operator=(const Sensors&) = delete;

    void Poll();
    int InternalGetPressure();
    bool InternalIsAirInLine();

    dmq::os::Thread m_thread{"SensorsThread", dmq::DEFAULT_QUEUE_SIZE, dmq::os::FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "Controller"};
    dmq::util::Timer m_pollTimer;
    dmq::ScopedConnection m_pollTimerConn;

    AirSensor m_air;
    PressureSensor m_pressure;
    RpmSensor m_rpm;
};


} // namespace sensors
} // namespace cellutron

#endif
