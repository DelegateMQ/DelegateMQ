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

void Sensors::GetPressure() {
    dmq::MakeDelegate(this, &Sensors::InternalGetPressure, m_thread).AsyncInvoke();
}

void Sensors::IsAirInLine() {
    dmq::MakeDelegate(this, &Sensors::InternalIsAirInLine, m_thread).AsyncInvoke();
}

void Sensors::InternalGetPressure() {
    int pressure = 0;
    dmq::databus::DataBus::Publish<SensorStatusMsg>(topics::STATUS_SENSOR, { SensorType::PRESSURE, (int16_t)pressure });
}

void Sensors::InternalIsAirInLine() {
    bool air = false;
    dmq::databus::DataBus::Publish<SensorStatusMsg>(topics::STATUS_SENSOR, { SensorType::AIR_IN_LINE, (int16_t)(air ? 1 : 0) });
}

} // namespace sensors
} // namespace cellutron
