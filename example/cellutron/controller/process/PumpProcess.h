/**
 * @file PumpProcess.h
 * @brief Atomic fluidics sequence state machine.
 * 
 * Handles the coordinated sequence of opening a valve, running a pump
 * for a specific duration, stopping the pump, and closing the valve.
 * Used as a sub-process by the CellProcess state machine.
 */

#ifndef _PUMP_PROCESS_H
#define _PUMP_PROCESS_H

#include "DelegateMQ.h"
#include "state-machine/StateMachine.h"
#include "extras/util/Timer.h"

namespace cellutron {
namespace process {

/// @brief Event data for PumpProcess containing all info needed for the sequence.
struct PumpData : public EventData
{
    uint8_t valveId;
    uint8_t speed;
    bool reverse;
    dmq::Duration duration;

    // Default to Pump ID 1 for all operations
    PumpData(uint8_t v, uint8_t s, dmq::Duration d, bool r = false) :
        valveId(v), speed(s), duration(d), reverse(r) {}
};

/// @brief Sub-process state machine that handles: Valve Open -> Pump On -> Wait -> Pump Off -> Valve Close.
class PumpProcess : public StateMachine
{
public:
    /// Signal emitted when the full sequence is complete.
    dmq::Signal<void()> OnComplete;

    PumpProcess();
    ~PumpProcess();

    virtual void SetThread(dmq::IThread& thread) override;

    /// Command the pump process to start a sequence.
    void Start(std::shared_ptr<const PumpData> data);

private:
    enum States
    {
        ST_IDLE,
        ST_VALVE_OPEN,
        ST_PUMP_ON,
        ST_WAITING,
        ST_PUMP_OFF,
        ST_VALVE_CLOSE,
        ST_COMPLETE,
        ST_MAX_STATES
    };

    // States
    STATE_DECLARE(PumpProcess, Idle, NoEventData)
    STATE_DECLARE(PumpProcess, ValveOpen, PumpData)
    STATE_DECLARE(PumpProcess, PumpOn, PumpData)
    STATE_DECLARE(PumpProcess, Waiting, PumpData)
    STATE_DECLARE(PumpProcess, PumpOff, PumpData)
    STATE_DECLARE(PumpProcess, ValveClose, PumpData)
    STATE_DECLARE(PumpProcess, Complete, NoEventData)

    // State map
    BEGIN_STATE_MAP
        STATE_MAP_ENTRY(&Idle)
        STATE_MAP_ENTRY(&ValveOpen)
        STATE_MAP_ENTRY(&PumpOn)
        STATE_MAP_ENTRY(&Waiting)
        STATE_MAP_ENTRY(&PumpOff)
        STATE_MAP_ENTRY(&ValveClose)
        STATE_MAP_ENTRY(&Complete)
    END_STATE_MAP

    // Event methods using transition maps
    void ValveOpened();
    void ValveClosed();
    void PumpStarted();
    void PumpStopped();
    void TimerDone();

    // Internal signal handlers
    void OnValveChanged(int id, bool open);
    void OnPumpChanged(int id, int speed);
    void OnTimerExpired();

    dmq::ScopedConnection m_valveConn;
    dmq::ScopedConnection m_pumpConn;
    dmq::ScopedConnection m_timerConn;
    dmq::util::Timer m_timer;
    std::shared_ptr<const PumpData> m_data;
};

} // namespace process
} // namespace cellutron

#endif // _PUMP_PROCESS_H
