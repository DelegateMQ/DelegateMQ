#include "PumpProcess.h"
#include "Logger.h"
#include "actuators/Actuators.h"
#include <cstdio>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {
namespace process {

PumpProcess::PumpProcess() :
    StateMachine(ST_MAX_STATES)
{
}

PumpProcess::~PumpProcess()
{
}

void PumpProcess::SetThread(dmq::IThread& thread)
{
    StateMachine::SetThread(thread);
    m_valveConn = actuators::Actuators::GetInstance().OnValveChanged.Connect(dmq::MakeDelegate(this, &PumpProcess::OnValveChanged, thread));
    m_pumpConn = actuators::Actuators::GetInstance().OnPumpChanged.Connect(dmq::MakeDelegate(this, &PumpProcess::OnPumpChanged, thread));
    m_timer.SetThread(&thread);
}

void PumpProcess::Start(std::shared_ptr<const cellutron::process::PumpData> data)
{
    BEGIN_TRANSITION_MAP(cellutron::process::PumpProcess, Start, data)
        TRANSITION_MAP_ENTRY(ST_VALVE_OPEN) // ST_IDLE
        TRANSITION_MAP_ENTRY(ST_VALVE_OPEN) // ST_VALVE_OPEN
        TRANSITION_MAP_ENTRY(ST_VALVE_OPEN) // ST_PUMP_ON
        TRANSITION_MAP_ENTRY(ST_VALVE_OPEN) // ST_WAITING
        TRANSITION_MAP_ENTRY(ST_VALVE_OPEN) // ST_PUMP_OFF
        TRANSITION_MAP_ENTRY(ST_VALVE_OPEN) // ST_VALVE_CLOSE
        TRANSITION_MAP_ENTRY(ST_VALVE_OPEN) // ST_COMPLETE
    END_TRANSITION_MAP(data)
}

void PumpProcess::OnValveChanged(int id, bool open) 
{
    if (m_data && id == m_data->valveId)
    {
        if (open) ValveOpened();
        else ValveClosed();
    }
}

void PumpProcess::OnPumpChanged(int id, int speed) 
{
    // We only have one pump (ID 1)
    if (m_data && id == 1)
    {
        int targetSpeed = m_data->reverse ? -static_cast<int>(m_data->speed) : static_cast<int>(m_data->speed);
        if (speed == targetSpeed) PumpStarted();
        else if (speed == 0) PumpStopped();
    }
}

void PumpProcess::OnTimerExpired() 
{
    TimerDone();
}

void PumpProcess::ValveOpened()
{
    BEGIN_TRANSITION_MAP(cellutron::process::PumpProcess, ValveOpened)
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_IDLE
        TRANSITION_MAP_ENTRY(ST_PUMP_ON)     // ST_VALVE_OPEN
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_PUMP_ON
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_WAITING
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_PUMP_OFF
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_VALVE_CLOSE
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_COMPLETE
    END_TRANSITION_MAP(m_data)
}

void PumpProcess::ValveClosed()
{
    BEGIN_TRANSITION_MAP(cellutron::process::PumpProcess, ValveClosed)
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_IDLE
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_VALVE_OPEN
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_PUMP_ON
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_WAITING
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_PUMP_OFF
        TRANSITION_MAP_ENTRY(ST_COMPLETE)    // ST_VALVE_CLOSE
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_COMPLETE
    END_TRANSITION_MAP(m_data)
}

void PumpProcess::PumpStarted()
{
    BEGIN_TRANSITION_MAP(cellutron::process::PumpProcess, PumpStarted)
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_IDLE
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_VALVE_OPEN
        TRANSITION_MAP_ENTRY(ST_WAITING)     // ST_PUMP_ON
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_WAITING
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_PUMP_OFF
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_VALVE_CLOSE
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_COMPLETE
    END_TRANSITION_MAP(m_data)
}

void PumpProcess::PumpStopped()
{
    BEGIN_TRANSITION_MAP(cellutron::process::PumpProcess, PumpStopped)
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_IDLE
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_VALVE_OPEN
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_PUMP_ON
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_WAITING
        TRANSITION_MAP_ENTRY(ST_VALVE_CLOSE) // ST_PUMP_OFF
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_VALVE_CLOSE
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_COMPLETE
    END_TRANSITION_MAP(m_data)
}

void PumpProcess::TimerDone()
{
    BEGIN_TRANSITION_MAP(cellutron::process::PumpProcess, TimerDone)
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_IDLE
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_VALVE_OPEN
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_PUMP_ON
        TRANSITION_MAP_ENTRY(ST_PUMP_OFF)    // ST_WAITING
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_PUMP_OFF
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_VALVE_CLOSE
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)  // ST_COMPLETE
    END_TRANSITION_MAP(m_data)
}

STATE_DEFINE(cellutron::process::PumpProcess, Idle, NoEventData)
{
    DMQ_LOG_INF("PumpProcess", "ST_IDLE");
    m_data = nullptr;
}

STATE_DEFINE(cellutron::process::PumpProcess, ValveOpen, cellutron::process::PumpData)
{
    m_data = data;
    m_timer.Stop();     // a new step: an abandoned step's wait never completes it
    DMQ_LOG_INF("PumpProcess", "ST_VALVE_OPEN (ID: %d)", data->valveId);
    actuators::Actuators::GetInstance().SetValve(data->valveId, true);
}

STATE_DEFINE(cellutron::process::PumpProcess, PumpOn, cellutron::process::PumpData)
{
    DMQ_LOG_INF("PumpProcess", "ST_PUMP_ON (Speed: %d%%, Reverse: %s)",
        data->speed, data->reverse ? "YES" : "NO");
    // Use fixed Pump ID 1. Apply negative speed for reverse direction.
    int targetSpeed = data->reverse ? -static_cast<int>(data->speed) : static_cast<int>(data->speed);
    actuators::Actuators::GetInstance().SetPump(1, targetSpeed);
}

STATE_DEFINE(cellutron::process::PumpProcess, Waiting, cellutron::process::PumpData)
{
    DMQ_LOG_INF("PumpProcess", "ST_WAITING (%lld ms)", (long long)data->duration.count());
    m_timer.Start(data->duration, dmq::MakeDelegate(this, &PumpProcess::OnTimerExpired));
}

STATE_DEFINE(cellutron::process::PumpProcess, PumpOff, cellutron::process::PumpData)
{
    DMQ_LOG_INF("PumpProcess", "ST_PUMP_OFF");
    // Use fixed Pump ID 1
    actuators::Actuators::GetInstance().SetPump(1, 0);
}

STATE_DEFINE(cellutron::process::PumpProcess, ValveClose, cellutron::process::PumpData)
{
    DMQ_LOG_INF("PumpProcess", "ST_VALVE_CLOSE (ID: %d)", data->valveId);
    actuators::Actuators::GetInstance().SetValve(data->valveId, false);
}

STATE_DEFINE(cellutron::process::PumpProcess, Complete, NoEventData)
{
    DMQ_LOG_INF("PumpProcess", "ST_COMPLETE");
    OnComplete();
    InternalEvent(ST_IDLE);
}

} // namespace process
} // namespace cellutron
