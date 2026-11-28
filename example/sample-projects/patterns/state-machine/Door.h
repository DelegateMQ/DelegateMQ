#ifndef DOOR_H
#define DOOR_H

// Door.h
// An automatic door controller built on active-fsm's StateMachine, running as
// an active object: every state action, entry/exit action and timer callback
// runs on the door's own dmq::os::Thread, so the class has no mutex.
//
//   CLOSED --Open--> OPENING --(travel time)--> OPEN --(auto-close)--> CLOSING
//     ^                 ^                                                 |
//     |                 +------------------- Obstacle --------------------+
//     +---------------------------- (travel time) ------------------------+
//
// Open(), Close() and Obstacle() may be called from any thread: the transition
// map posts the call to the door's thread (BEGIN_TRANSITION_MAP) and returns.

#include "StateMachine.h"
#include "extras/util/OneShotTimer.h"

class Door : public StateMachine
{
public:
    enum States : uint8_t
    {
        ST_CLOSED,
        ST_OPENING,
        ST_OPEN,
        ST_CLOSING,
        ST_MAX_STATES
    };

    /// Time the motor takes to open or close the door.
    static constexpr dmq::Duration TRAVEL_TIME = std::chrono::milliseconds(50);

    /// How long the door stays open before it closes on its own.
    static constexpr dmq::Duration AUTO_CLOSE_TIME = std::chrono::milliseconds(100);

    Door();
    ~Door();

    /// Stop the door's thread. Call before the timer driver stops, and before
    /// anything the door's subscribers use is destroyed.
    void Shutdown();

    static const char* StateName(uint8_t state);

    // External events, callable from any thread
    void Open();
    void Close();
    void Obstacle();

private:
    // Internal events, raised on the door's thread by its timers
    void MotionDone();
    void AutoClose();

    dmq::os::Thread m_thread;

    // One-shot timers whose callbacks run on the door's thread, so never
    // concurrently with a state action. An expiry from a timer the door has
    // since stopped (the state it was started for was left) never runs, even
    // if it was already queued to the thread.
    dmq::util::OneShotTimer m_motionTimer{ &m_thread };
    dmq::util::OneShotTimer m_autoCloseTimer{ &m_thread };

    // State functions
    STATE_DECLARE(Door, Closed, NoEventData)
    STATE_DECLARE(Door, Opening, NoEventData)
    STATE_DECLARE(Door, OpenState, NoEventData)
    STATE_DECLARE(Door, Closing, NoEventData)

    // Entry and exit actions
    ENTRY_DECLARE(Door, EnterMoving, NoEventData)
    EXIT_DECLARE(Door, ExitMoving)
    ENTRY_DECLARE(Door, EnterOpen, NoEventData)
    EXIT_DECLARE(Door, ExitOpen)

    BEGIN_STATE_MAP_EX
        STATE_MAP_ENTRY_EX(&Closed)
        STATE_MAP_ENTRY_ALL_EX(&Opening, nullptr, &EnterMoving, &ExitMoving)
        STATE_MAP_ENTRY_ALL_EX(&OpenState, nullptr, &EnterOpen, &ExitOpen)
        STATE_MAP_ENTRY_ALL_EX(&Closing, nullptr, &EnterMoving, &ExitMoving)
    END_STATE_MAP_EX
};

#endif // DOOR_H
