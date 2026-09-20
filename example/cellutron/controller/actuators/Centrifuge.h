#ifndef _CENTRIFUGE_H
#define _CENTRIFUGE_H

#include "DelegateMQ.h"
#include "state-machine/StateMachine.h"
#include "extras/util/Timer.h"

namespace cellutron {
namespace actuators {

/// @brief Centrifuge state machine simulates the physics of the centrifuge motor.
class Centrifuge : public StateMachine
{
public:
    /// Signal fired when the target RPM is reached.
    dmq::Signal<void(uint16_t rpm)> OnTargetReached;

    // Move struct here so it's available for the methods and macros
    struct RampData : public EventData {
        RampData(uint16_t r, dmq::Duration d) : targetRpm(r), duration(d) {}
        uint16_t targetRpm;
        dmq::Duration duration;
    };

    Centrifuge();
    ~Centrifuge();

    virtual void SetThread(dmq::IThread& thread) override;

    /// Command a ramp to target speed.
    void StartRamp(std::shared_ptr<const RampData> data);

    /// Command an immediate stop (ramp to 0).
    void StopRamp(std::shared_ptr<const RampData> data);

private:
    enum States
    {
        ST_IDLE,
        ST_START_RAMP,
        ST_RAMPING,
        ST_RAMP_STEP,
        ST_AT_SPEED,
        ST_START_STOP,
        ST_STOPPING,
        ST_STOP_STEP,
        ST_MAX_STATES
    };

    STATE_DECLARE(Centrifuge, Idle, NoEventData)
    STATE_DECLARE(Centrifuge, StartRampState, RampData)
    STATE_DECLARE(Centrifuge, Ramping, RampData)
    STATE_DECLARE(Centrifuge, RampStep, RampData)
    STATE_DECLARE(Centrifuge, AtSpeed, NoEventData)
    STATE_DECLARE(Centrifuge, StartStopState, RampData)
    STATE_DECLARE(Centrifuge, Stopping, RampData)
    STATE_DECLARE(Centrifuge, StopStep, RampData)

    BEGIN_STATE_MAP
        STATE_MAP_ENTRY(&Idle)
        STATE_MAP_ENTRY(&StartRampState)
        STATE_MAP_ENTRY(&Ramping)
        STATE_MAP_ENTRY(&RampStep)
        STATE_MAP_ENTRY(&AtSpeed)
        STATE_MAP_ENTRY(&StartStopState)
        STATE_MAP_ENTRY(&Stopping)
        STATE_MAP_ENTRY(&StopStep)
    END_STATE_MAP

    void Poll();
    void StartPoll() { m_pollTimer.Start(std::chrono::milliseconds(100)); }
    void StopPoll() { m_pollTimer.Stop(); }

    uint16_t m_currentRpm = 0;
    uint16_t m_startRpm = 0;
    uint16_t m_targetRpm = 0;
    dmq::Duration m_rampDuration = std::chrono::milliseconds(0);
    dmq::TimePoint m_rampStartTime;

    dmq::util::Timer m_pollTimer;
    dmq::ScopedConnection m_pollTimerConn;
    std::shared_ptr<const RampData> m_data;
};

} // namespace actuators
} // namespace cellutron

#endif
