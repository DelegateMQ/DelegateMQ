#include "Sensors.h"
#include "messages/SensorStatusMsg.h"
#include "util/Constants.h"
#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {
namespace sensors {

Sensors::~Sensors() {
    Shutdown();
}

void Sensors::Initialize() {
    // Enable DelegateMQ Watchdog (2 second timeout)
    ThreadMonitor::Register(&m_thread);
    m_thread.SetThreadPriority(PRIORITY_HARDWARE);
    m_thread.CreateThread(WATCHDOG_TIMEOUT);
    printf("Sensors: Subsystem initialized.\n");
}

void Sensors::Shutdown() {
    m_thread.ExitThread();
}

int Sensors::GetPressure() {
    auto result = dmq::MakeDelegate(this, &Sensors::InternalGetPressure, m_thread, SYNC_INVOKE_TIMEOUT).AsyncInvoke();
    return result.has_value() ? result.value() : -1; // Return -1 on failure
}

bool Sensors::IsAirInLine() {
    auto result = dmq::MakeDelegate(this, &Sensors::InternalIsAirInLine, m_thread, SYNC_INVOKE_TIMEOUT).AsyncInvoke();
    return result.has_value() ? result.value() : true; // Fail safe: assume air if call fails
}

int Sensors::InternalGetPressure() {
    int pressure = 0;
    dmq::databus::DataBus::Publish<SensorStatusMsg>(topics::STATUS_SENSOR, { SensorType::PRESSURE, (int16_t)pressure });
    return pressure; 
}

bool Sensors::InternalIsAirInLine() {
    bool air = false;
    dmq::databus::DataBus::Publish<SensorStatusMsg>(topics::STATUS_SENSOR, { SensorType::AIR_IN_LINE, (int16_t)(air ? 1 : 0) });
    return air;
}

} // namespace sensors
} // namespace cellutron
