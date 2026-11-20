#include "Door.h"

using namespace dmq;
using dmq::util::Timer;

Door::Door() :
    StateMachine(ST_MAX_STATES, ST_CLOSED),
    m_thread("DoorThread")
{
    m_thread.CreateThread();

    // Active-object mode: events called from other threads are posted here
    SetThread(m_thread);

    // Timers expire on the timer driver's thread. Connecting them with the
    // door's thread queues each callback to the door, so the callbacks
    // never run concurrently with a state action.
    m_motionConn = m_motionTimer.OnExpired.Connect(
        MakeDelegate(this, &Door::OnMotionTimer, m_thread));
    m_autoCloseConn = m_autoCloseTimer.OnExpired.Connect(
        MakeDelegate(this, &Door::OnAutoCloseTimer, m_thread));
}

Door::~Door()
{
    Shutdown();
}

void Door::Shutdown()
{
    m_motionTimer.Stop();
    m_autoCloseTimer.Stop();
    m_motionConn.Disconnect();
    m_autoCloseConn.Disconnect();
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

// Runs on the door's thread
void Door::OnMotionTimer()
{
    // Discard an expiry from a motion that was reversed after the timer fired
    if (Timer::GetNow() - m_stateEntered >= TRAVEL_TIME)
        MotionDone();
}

// Runs on the door's thread
void Door::OnAutoCloseTimer()
{
    // Discard an expiry from an earlier visit to ST_OPEN
    if (Timer::GetNow() - m_stateEntered >= AUTO_CLOSE_TIME)
        AutoClose();
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
    // Stamp before Start(), so a timer that expires on time always passes the
    // elapsed-time check in its callback
    m_stateEntered = Timer::GetNow();
    m_motionTimer.Start(TRAVEL_TIME, /*once=*/true);
}

EXIT_DEFINE(Door, ExitMoving)
{
    m_motionTimer.Stop();
}

ENTRY_DEFINE(Door, EnterOpen, NoEventData)
{
    m_stateEntered = Timer::GetNow();
    m_autoCloseTimer.Start(AUTO_CLOSE_TIME, /*once=*/true);
}

EXIT_DEFINE(Door, ExitOpen)
{
    m_autoCloseTimer.Stop();
}
