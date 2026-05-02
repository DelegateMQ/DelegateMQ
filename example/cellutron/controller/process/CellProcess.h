/**
 * @file CellProcess.h
 * @brief High-level cell processing sequence state machine.
 * 
 * Orchestrates the full lifecycle of a cell processing run, including
 * filling solutions, cell loading, centrifuge spinning, and draining.
 * It coordinates with the PumpProcess for fluidics and Centrifuge for speed control.
 */

#ifndef _CELL_PROCESS_H
#define _CELL_PROCESS_H

#include "DelegateMQ.h"
#include "state-machine/StateMachine.h"
#include "actuators/Centrifuge.h"
#include <chrono>

namespace cellutron {
namespace process {

class PumpProcess;

/// @brief CellProcess state machine controls the high-level cell processing steps.
class CellProcess : public StateMachine
{
public:
    using StateMachine::OnTransition;
    using StateMachine::OnEntry;
    using StateMachine::OnExit;
    using StateMachine::OnCannotHappen;

    enum States
    {
        ST_IDLE,
        ST_FILL_SOLUTION_A,
        ST_FILL_SOLUTION_B,
        ST_FILL_CELLS,
        ST_SPIN_UP,
        ST_SPIN_DOWN,
        ST_DRAIN,
        ST_COMPLETE,
        ST_ABORTING,
        ST_FAULT,
        ST_MAX_STATES
    };

    CellProcess(actuators::Centrifuge& centrifuge, PumpProcess& pumpProcess);
    ~CellProcess();

    virtual void SetThread(dmq::IThread& thread) override;

    /// Start the cell process.
    void StartProcess();

    /// Stop/Abort the cell process.
    void AbortProcess();

    /// Fault event - transitions to ST_FAULT.
    void GenerateFault();

protected:
    // States
    STATE_DECLARE(CellProcess, Idle, NoEventData)
    STATE_DECLARE(CellProcess, FillSolutionA, NoEventData)
    STATE_DECLARE(CellProcess, FillSolutionB, NoEventData)
    STATE_DECLARE(CellProcess, FillCells, NoEventData)
    STATE_DECLARE(CellProcess, SpinUp, NoEventData)
    STATE_DECLARE(CellProcess, SpinDown, NoEventData)
    STATE_DECLARE(CellProcess, Drain, NoEventData)
    STATE_DECLARE(CellProcess, Complete, NoEventData)
    STATE_DECLARE(CellProcess, Aborting, NoEventData)
    STATE_DECLARE(CellProcess, Fault, NoEventData)

    // State map
    BEGIN_STATE_MAP
        STATE_MAP_ENTRY(&Idle)
        STATE_MAP_ENTRY(&FillSolutionA)
        STATE_MAP_ENTRY(&FillSolutionB)
        STATE_MAP_ENTRY(&FillCells)
        STATE_MAP_ENTRY(&SpinUp)
        STATE_MAP_ENTRY(&SpinDown)
        STATE_MAP_ENTRY(&Drain)
        STATE_MAP_ENTRY(&Complete)
        STATE_MAP_ENTRY(&Aborting)
        STATE_MAP_ENTRY(&Fault)
    END_STATE_MAP

private:
    // External events
    void CentrifugeAtSpeed();
    void CentrifugeStopped();
    void ValveChanged(int id, bool open);
    void PumpChanged(int id, int speed);
    void TimerExpired();
    void PumpComplete();
    void FaultEvent();

    void OnCentrifugeTargetReached(uint16_t rpm);
    void OnValveChanged(int id, bool open);
    void OnPumpChanged(int id, int speed);
    void OnTimerExpired();
    void OnPumpComplete();

    actuators::Centrifuge& m_centrifuge;
    PumpProcess& m_pumpProcess;
    dmq::ScopedConnection m_centrifugeConn;
    dmq::ScopedConnection m_valveConn;
    dmq::ScopedConnection m_pumpConn;
    dmq::ScopedConnection m_timerConn;
    dmq::ScopedConnection m_pumpCompleteConn;
    dmq::ScopedConnection m_transitionConn;

    dmq::util::Timer m_timer;
    bool  m_newChange = false;
    std::chrono::steady_clock::time_point m_processStartTime;
};

} // namespace process
} // namespace cellutron

#endif
