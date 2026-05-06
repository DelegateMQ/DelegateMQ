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
    // 1. Register for monitoring and start thread
    ThreadMonitor::Register(&m_thread);
    m_thread.SetThreadPriority(PRIORITY_HARDWARE);
    m_thread.CreateThread(WATCHDOG_TIMEOUT);

    // 2. Initialize simulated sensors
    m_rpm.Initialize(m_thread);

    // 3. Setup 50ms hardware polling loop
    m_pollTimerConn = m_pollTimer.OnExpired.Connect(dmq::util::MakeTimerDelegate(this, &Sensors::Poll, m_thread));
    m_pollTimer.Start(std::chrono::milliseconds(50));

    printf("Sensors: Subsystem initialized.\n");
}

void Sensors::Shutdown() {
    m_pollTimer.Stop();
    m_pollTimerConn.Disconnect();
    m_thread.ExitThread();
}

void Sensors::Poll() {
    // Update all simulated hardware values and publish to DataBus
    m_air.Poll();
    m_pressure.Poll();
    m_rpm.Poll();
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
    int pressure = m_pressure.GetInlet();
    dmq::databus::DataBus::Publish<SensorStatusMsg>(topics::STATUS_SENSOR, { SensorType::PRESSURE, (int16_t)pressure });
    return pressure; 
}

bool Sensors::InternalIsAirInLine() {
    bool air = m_air.IsInletAir();
    dmq::databus::DataBus::Publish<SensorStatusMsg>(topics::STATUS_SENSOR, { SensorType::AIR_IN_LINE, (int16_t)(air ? 1 : 0) });
    return air;
}

} // namespace sensors
} // namespace cellutron
