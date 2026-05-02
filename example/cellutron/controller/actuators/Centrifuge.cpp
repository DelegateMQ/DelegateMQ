#include "Centrifuge.h"
#include "messages/CentrifugeSpeedMsg.h"
#include "util/Constants.h"
#include <algorithm>
#include <iostream>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace cellutron;

namespace cellutron {
namespace actuators {

Centrifuge::Centrifuge() :
    StateMachine(ST_MAX_STATES)
{
}

Centrifuge::~Centrifuge()
{
    m_pollTimer.Stop();
}

void Centrifuge::SetThread(dmq::IThread& thread)
{
    StateMachine::SetThread(thread);
    m_pollTimerConn = m_pollTimer.OnExpired.Connect(dmq::util::MakeTimerDelegate(this, &Centrifuge::Poll, thread));
}

void Centrifuge::StartRamp(std::shared_ptr<const cellutron::actuators::Centrifuge::RampData> data)
{
    BEGIN_TRANSITION_MAP(cellutron::actuators::Centrifuge, StartRamp, data)
        TRANSITION_MAP_ENTRY(ST_START_RAMP) // ST_IDLE
        TRANSITION_MAP_ENTRY(ST_START_RAMP) // ST_START_RAMP
        TRANSITION_MAP_ENTRY(ST_START_RAMP) // ST_RAMPING
        TRANSITION_MAP_ENTRY(ST_START_RAMP) // ST_RAMP_STEP
        TRANSITION_MAP_ENTRY(ST_START_RAMP) // ST_AT_SPEED
        TRANSITION_MAP_ENTRY(ST_START_RAMP) // ST_START_STOP
        TRANSITION_MAP_ENTRY(ST_START_RAMP) // ST_STOPPING
        TRANSITION_MAP_ENTRY(ST_START_RAMP) // ST_STOP_STEP
    END_TRANSITION_MAP(data)
}

void Centrifuge::StopRamp(std::shared_ptr<const cellutron::actuators::Centrifuge::RampData> data)
{
    BEGIN_TRANSITION_MAP(cellutron::actuators::Centrifuge, StopRamp, data)
        TRANSITION_MAP_ENTRY(ST_START_STOP) // ST_IDLE
        TRANSITION_MAP_ENTRY(ST_START_STOP) // ST_START_RAMP
        TRANSITION_MAP_ENTRY(ST_START_STOP) // ST_RAMPING
        TRANSITION_MAP_ENTRY(ST_START_STOP) // ST_RAMP_STEP
        TRANSITION_MAP_ENTRY(ST_START_STOP) // ST_AT_SPEED
        TRANSITION_MAP_ENTRY(ST_START_STOP) // ST_START_STOP
        TRANSITION_MAP_ENTRY(ST_START_STOP) // ST_STOPPING
        TRANSITION_MAP_ENTRY(ST_START_STOP) // ST_STOP_STEP
    END_TRANSITION_MAP(data)
}

void Centrifuge::Poll()
{
    BEGIN_TRANSITION_MAP(cellutron::actuators::Centrifuge, Poll)
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)    // ST_IDLE
        TRANSITION_MAP_ENTRY(ST_RAMPING)       // ST_START_RAMP
        TRANSITION_MAP_ENTRY(ST_RAMP_STEP)     // ST_RAMPING
        TRANSITION_MAP_ENTRY(ST_RAMPING)       // ST_RAMP_STEP
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)    // ST_AT_SPEED
        TRANSITION_MAP_ENTRY(ST_STOPPING)      // ST_START_STOP
        TRANSITION_MAP_ENTRY(ST_STOP_STEP)     // ST_STOPPING
        TRANSITION_MAP_ENTRY(ST_STOPPING)      // ST_STOP_STEP
    END_TRANSITION_MAP(m_data)
}

STATE_DEFINE(cellutron::actuators::Centrifuge, Idle, NoEventData)
{
    printf("Centrifuge: ST_IDLE\n");
    bool wasRamping = (m_currentRpm != 0);
    m_currentRpm = 0;
    StopPoll();
    m_data = nullptr;
    dmq::databus::DataBus::Publish<CentrifugeSpeedMsg>(topics::CMD_CENTRIFUGE_SPEED, { 0 });
    
    if (wasRamping) {
        OnTargetReached(0);
    }
}

STATE_DEFINE(cellutron::actuators::Centrifuge, StartRampState, cellutron::actuators::Centrifuge::RampData)
{
    printf("Centrifuge: ST_START_RAMP (Target: %u RPM)\n", data->targetRpm);
    m_data = data;
    m_startRpm = m_currentRpm;
    m_targetRpm = data->targetRpm;
    m_rampDuration = data->duration;
    m_rampStartTime = Clock::now();

    if (m_rampDuration.count() > 0)
    {
        StartPoll();
        InternalEvent(ST_RAMPING, data);
    }
    else
    {
        m_currentRpm = m_targetRpm;
        InternalEvent(ST_AT_SPEED);
    }
}

STATE_DEFINE(cellutron::actuators::Centrifuge, Ramping, cellutron::actuators::Centrifuge::RampData)
{
    auto now = Clock::now();
    auto elapsed = now - m_rampStartTime;

    if (elapsed >= m_rampDuration)
    {
        printf("Centrifuge: ST_RAMPING (Complete)\n");
        m_currentRpm = m_targetRpm;
        InternalEvent(ST_AT_SPEED);
    }
    else
    {
        float progress = static_cast<float>(elapsed.count()) / m_rampDuration.count();
        m_currentRpm = static_cast<uint16_t>(m_startRpm + (static_cast<int32_t>(m_targetRpm) - m_startRpm) * progress);
        printf("Centrifuge: ST_RAMPING (%u RPM)\n", m_currentRpm);
        dmq::databus::DataBus::Publish<CentrifugeSpeedMsg>(topics::CMD_CENTRIFUGE_SPEED, { m_currentRpm });
    }
}

STATE_DEFINE(cellutron::actuators::Centrifuge, RampStep, cellutron::actuators::Centrifuge::RampData)
{
    InternalEvent(ST_RAMPING, data);
}

STATE_DEFINE(cellutron::actuators::Centrifuge, AtSpeed, NoEventData)
{
    printf("Centrifuge: ST_AT_SPEED (%u RPM)\n", m_currentRpm);
    StopPoll();
    dmq::databus::DataBus::Publish<CentrifugeSpeedMsg>(topics::CMD_CENTRIFUGE_SPEED, { m_currentRpm });
    OnTargetReached(m_currentRpm);
}

STATE_DEFINE(cellutron::actuators::Centrifuge, StartStopState, cellutron::actuators::Centrifuge::RampData)
{
    printf("Centrifuge: ST_START_STOP\n");
    m_data = data;
    m_startRpm = m_currentRpm;
    m_targetRpm = 0;
    m_rampDuration = data->duration;
    m_rampStartTime = Clock::now();
    StartPoll();
    InternalEvent(ST_STOPPING, data);
}

STATE_DEFINE(cellutron::actuators::Centrifuge, Stopping, cellutron::actuators::Centrifuge::RampData)
{
    auto now = Clock::now();
    auto elapsed = now - m_rampStartTime;

    if (elapsed >= m_rampDuration)
    {
        printf("Centrifuge: ST_STOPPING (Complete)\n");
        InternalEvent(ST_IDLE);
    }
    else
    {
        float progress = static_cast<float>(elapsed.count()) / m_rampDuration.count();
        m_currentRpm = static_cast<uint16_t>(m_startRpm + (static_cast<int32_t>(m_targetRpm) - m_startRpm) * progress);
        printf("Centrifuge: ST_STOPPING (%u RPM)\n", m_currentRpm);
        dmq::databus::DataBus::Publish<CentrifugeSpeedMsg>(topics::CMD_CENTRIFUGE_SPEED, { m_currentRpm });
    }
}

STATE_DEFINE(cellutron::actuators::Centrifuge, StopStep, cellutron::actuators::Centrifuge::RampData)
{
    InternalEvent(ST_STOPPING, data);
}

} // namespace actuators
} // namespace cellutron
