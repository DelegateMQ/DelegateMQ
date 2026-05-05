#ifndef _SENSORS_H
#define _SENSORS_H

#include "DelegateMQ.h"

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

    /// Request current pressure (Non-blocking)
    void GetPressure();

    /// Request air detection status (Non-blocking)
    void IsAirInLine();

private:
    Sensors() = default;
    ~Sensors();

    Sensors(const Sensors&) = delete;
    Sensors& operator=(const Sensors&) = delete;

    void InternalGetPressure();
    void InternalIsAirInLine();

    dmq::os::Thread m_thread{"SensorsThread", 50, dmq::os::FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "Controller"};
};

} // namespace sensors
} // namespace cellutron

#endif
