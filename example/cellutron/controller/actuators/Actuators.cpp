#include "Actuators.h"
#include "messages/ActuatorStatusMsg.h"
#include "util/Constants.h"
#include <cstdio>

#include "FreeRTOS.h"
#include "task.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {
namespace actuators {

Actuators::~Actuators() {
    Shutdown();
}

void Actuators::Initialize() {
    // 1. Start the thread
    ThreadMonitor::Register(&m_thread);
    m_thread.SetThreadPriority(PRIORITY_HARDWARE);
    m_thread.CreateThread(WATCHDOG_TIMEOUT);

    // 2. Initialize Centrifuge
    m_centrifuge.SetThread(m_thread);

    // 3. Initialize Valves (IDs used in CellProcess: 1, 2, 3, 4)
    int valveIds[] = {1, 2, 3, 4};
    for (int id : valveIds) {
        m_valves.emplace(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(id));
        
        // Connect internal valve signal to Actuators signal. 
        // valve::OnStateChanged(int, bool) connects to Actuators::HandleValveChanged(int, bool)
        m_valveConns[id] = m_valves.at(id).OnStateChanged.Connect(
            dmq::MakeDelegate(this, &Actuators::HandleValveChanged, m_thread));
    }

    // 4. Initialize Single Pump (ID 1)
    m_pumps.emplace(std::piecewise_construct, std::forward_as_tuple(1), std::forward_as_tuple(1));

    // Connect internal pump signal to Actuators signal.
    // pump::OnSpeedChanged(int, uint8_t) connects to Actuators::HandlePumpChanged(int, uint8_t)
    m_pumpConns[1] = m_pumps.at(1).OnSpeedChanged.Connect(
        dmq::MakeDelegate(this, &Actuators::HandlePumpChanged, m_thread));

    printf("Actuators: Subsystem initialized.\n");
}

void Actuators::HandleValveChanged(int id, bool open) {
    OnValveChanged(id, open);
    //(void)dmq::MakeDelegate(&OnValveChanged, &dmq::Signal<void(int id, bool open)>::operator(), m_thread).AsyncInvoke(id, open);
}

void Actuators::HandlePumpChanged(int id, int speed) {
    OnPumpChanged(id, speed);
    //(void)dmq::MakeDelegate(&OnPumpChanged, &dmq::Signal<void(int id, int speed)>::operator(), m_thread).AsyncInvoke(id, speed);
}

void Actuators::Shutdown() {
    m_thread.ExitThread();
}

void Actuators::SetValve(int id, bool open) {
    dmq::MakeDelegate(this, &Actuators::InternalSetValve, m_thread).AsyncInvoke(id, open);
}

void Actuators::SetPump(int id, int speed) {
    dmq::MakeDelegate(this, &Actuators::InternalSetPump, m_thread).AsyncInvoke(id, speed);
}

void Actuators::InternalSetValve(int id, bool open) {
    try {
        m_valves.at(id).SetState(open);
    } catch (const std::out_of_range&) {
        printf("Actuators: ERROR - Valve ID %d not found!\n", id);
        return;
    }
    
    Thread::Sleep(std::chrono::milliseconds(100));
}

void Actuators::InternalSetPump(int id, int speed) {
    try {
        m_pumps.at(id).SetSpeed(speed);
    } catch (const std::out_of_range&) {
        printf("Actuators: ERROR - Pump ID %d not found!\n", id);
        return;
    }
    
    Thread::Sleep(std::chrono::milliseconds(100));
}

} // namespace actuators
} // namespace cellutron
