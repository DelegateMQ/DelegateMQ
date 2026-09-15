#include "StateMachine.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

//----------------------------------------------------------------------------
// StateMachine
//----------------------------------------------------------------------------
StateMachine::StateMachine(uint8_t maxStates, uint8_t initialState) :
    MAX_STATES(maxStates),
    m_currentState(initialState),
    m_newState(0),
    m_eventGenerated(false),
    m_pEventData(nullptr)
{
    DMQ_ASSERT_TRUE(MAX_STATES < EVENT_IGNORED);
}  

//----------------------------------------------------------------------------
// ExternalEvent
//----------------------------------------------------------------------------
void StateMachine::ExternalEvent(uint8_t newState, std::shared_ptr<const EventData> pData)
{
    // If we are supposed to ignore this event
    if (newState == EVENT_IGNORED)
    {
    }
    else
    {
        if (newState == CANNOT_HAPPEN)
        {
            OnCannotHappen(m_currentState);
            DMQ_ASSERT();
        }

        if (!IsOnStateMachineThread())
        {
            MakeDelegate(this, &StateMachine::ExternalEventImpl, *m_thread)(newState, pData);
        }
        else
            ExternalEventImpl(newState, pData);
    }
}

//----------------------------------------------------------------------------
// ExternalEventImpl
//----------------------------------------------------------------------------
void StateMachine::ExternalEventImpl(uint8_t newState, std::shared_ptr<const EventData> pData)
{
    // Generate the event
    InternalEvent(newState, pData);

    // Execute the state engine. This function call will only return
    // when all state machine events are processed.
    StateEngine();
}

//----------------------------------------------------------------------------
// InternalEvent
//----------------------------------------------------------------------------
void StateMachine::InternalEvent(uint8_t newState, std::shared_ptr<const EventData> pData)
{
    if (pData == nullptr)
        pData = xmake_shared<NoEventData>();

    m_pEventData = pData;
    m_eventGenerated = true;
    m_newState = newState;
}

//----------------------------------------------------------------------------
// IsOnStateMachineThread
//----------------------------------------------------------------------------
bool StateMachine::IsOnStateMachineThread() const
{
    if (!m_thread)
        return true;
    return m_thread->IsCurrentThread();
}

//----------------------------------------------------------------------------
// StateEngine
//----------------------------------------------------------------------------
void StateMachine::StateEngine(void)
{
    const StateMapRow* pStateMap = GetStateMap();
    if (pStateMap != nullptr)
        StateEngine(pStateMap);
    else
    {
        const StateMapRowEx* pStateMapEx = GetStateMapEx();
        if (pStateMapEx != nullptr)
            StateEngine(pStateMapEx);
        else
            DMQ_ASSERT();
    }
}

//----------------------------------------------------------------------------
// StateEngine
//----------------------------------------------------------------------------
void StateMachine::StateEngine(const StateMapRow* const pStateMap)
{
    std::shared_ptr<const EventData> pDataTemp = nullptr;	

    // While events are being generated keep executing states
    while (m_eventGenerated)
    {
        // Error check that the new state is valid before proceeding
        DMQ_ASSERT_TRUE(m_newState < MAX_STATES);

        // Get the pointer from the state map
        const StateBase* state = pStateMap[m_newState].State;

        // Copy of event data pointer
        pDataTemp = m_pEventData;

        // Event data used up, reset the pointer
        m_pEventData = nullptr;

        // Event used up, reset the flag
        m_eventGenerated = false;

        uint8_t fromState = m_currentState;

        // Switch to the new current state
        SetCurrentState(m_newState);

        // Execute the state action passing in event data
        DMQ_ASSERT_TRUE(state != nullptr);
        state->InvokeStateAction(this, pDataTemp);

        // Fire transition signal
        OnTransition(fromState, m_currentState);
    }
}

//----------------------------------------------------------------------------
// StateEngine
//----------------------------------------------------------------------------
void StateMachine::StateEngine(const StateMapRowEx* const pStateMapEx)
{
    std::shared_ptr<const EventData> pDataTemp = nullptr;

    // While events are being generated keep executing states
    while (m_eventGenerated)
    {
        // Error check that the new state is valid before proceeding
        DMQ_ASSERT_TRUE(m_newState < MAX_STATES);

        // Get the pointers from the state map
        const StateBase* state = pStateMapEx[m_newState].State;
        const GuardBase* guard = pStateMapEx[m_newState].Guard;
        const EntryBase* entry = pStateMapEx[m_newState].Entry;
        const ExitBase* exit = pStateMapEx[m_currentState].Exit;

        // Copy of event data pointer
        pDataTemp = m_pEventData;

        // Event data used up, reset the pointer
        m_pEventData = nullptr;

        // Event used up, reset the flag
        m_eventGenerated = false;

        // Execute the guard condition
        bool guardResult = true;
        if (guard != nullptr)
            guardResult = guard->InvokeGuardCondition(this, pDataTemp);

        // If the guard condition succeeds
        if (guardResult == true)
        {
            uint8_t fromState = m_currentState;
            // Transitioning to a new state?
            if (m_newState != m_currentState)
            {
                // Execute the state exit action on current state before switching to new state
                if (exit != nullptr)
                    exit->InvokeExitAction(this);
                
                OnExit(m_currentState);

                // Switch to the new current state
                SetCurrentState(m_newState);

                // Execute the state entry action on the new state
                if (entry != nullptr)
                    entry->InvokeEntryAction(this, pDataTemp);

                OnEntry(m_currentState);

                // Ensure exit/entry actions didn't call InternalEvent by accident 
                DMQ_ASSERT_TRUE(m_eventGenerated == false);
            }
            else
            {
                // Self-transition
                SetCurrentState(m_newState);
            }

            // Execute the state action passing in event data
            DMQ_ASSERT_TRUE(state != nullptr);
            state->InvokeStateAction(this, pDataTemp);

            // Fire transition signal
            OnTransition(fromState, m_currentState);
        }
    }
}
