#include "Door.h"

using namespace dmq;

Door::Door() :
    StateMachine(ST_MAX_STATES, ST_CLOSED),
    m_thread("DoorThread")
{
    m_thread.CreateThread();

    // Active-object mode: events called from other threads are posted here
    SetThread(m_thread);
}

Door::~Door()
{
    Shutdown();
}

void Door::Shutdown()
{
    m_motionTimer.Stop();
    m_autoCloseTimer.Stop();
    m_thread.ExitThread();
}

const char* Door::StateName(uint8_t state)
{
    switch (state)
    {
    case ST_CLOSED:  return "CLOSED";
    case ST_OPENING: return "OPENING";
    case ST_OPEN:    return "OPEN";
    case ST_CLOSING: return "CLOSING";
    default:         return "?";
    }
}

void Door::Open()
{
    BEGIN_TRANSITION_MAP(Door, Open)                // - Current State -
        TRANSITION_MAP_ENTRY(ST_OPENING)            // ST_CLOSED
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_OPENING
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_OPEN
        TRANSITION_MAP_ENTRY(ST_OPENING)            // ST_CLOSING
    END_TRANSITION_MAP(nullptr)
}

void Door::Close()
{
    BEGIN_TRANSITION_MAP(Door, Close)               // - Current State -
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_CLOSED
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_OPENING (finish opening first)
        TRANSITION_MAP_ENTRY(ST_CLOSING)            // ST_OPEN
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_CLOSING
    END_TRANSITION_MAP(nullptr)
}

void Door::Obstacle()
{
    BEGIN_TRANSITION_MAP(Door, Obstacle)            // - Current State -
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_CLOSED
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_OPENING
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_OPEN
        TRANSITION_MAP_ENTRY(ST_OPENING)            // ST_CLOSING (reverse)
    END_TRANSITION_MAP(nullptr)
}

void Door::MotionDone()
{
    BEGIN_TRANSITION_MAP(Door, MotionDone)          // - Current State -
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_CLOSED
        TRANSITION_MAP_ENTRY(ST_OPEN)               // ST_OPENING
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_OPEN
        TRANSITION_MAP_ENTRY(ST_CLOSED)             // ST_CLOSING
    END_TRANSITION_MAP(nullptr)
}

void Door::AutoClose()
{
    BEGIN_TRANSITION_MAP(Door, AutoClose)           // - Current State -
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_CLOSED
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_OPENING
        TRANSITION_MAP_ENTRY(ST_CLOSING)            // ST_OPEN
        TRANSITION_MAP_ENTRY(EVENT_IGNORED)         // ST_CLOSING
    END_TRANSITION_MAP(nullptr)
}

// The state actions have nothing to do: the work is in the entry and exit
// actions, which start and stop the timers.
STATE_DEFINE(Door, Closed, NoEventData)
{
}

STATE_DEFINE(Door, Opening, NoEventData)
{
}

STATE_DEFINE(Door, OpenState, NoEventData)
{
}

STATE_DEFINE(Door, Closing, NoEventData)
{
}

ENTRY_DEFINE(Door, EnterMoving, NoEventData)
{
    m_motionTimer.Start(TRAVEL_TIME, MakeDelegate(this, &Door::MotionDone));
}

EXIT_DEFINE(Door, ExitMoving)
{
    m_motionTimer.Stop();
}

ENTRY_DEFINE(Door, EnterOpen, NoEventData)
{
    m_autoCloseTimer.Start(AUTO_CLOSE_TIME, MakeDelegate(this, &Door::AutoClose));
}

EXIT_DEFINE(Door, ExitOpen)
{
    m_autoCloseTimer.Stop();
}
