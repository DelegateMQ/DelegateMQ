#ifndef _STATE_MACHINE_H
#define _STATE_MACHINE_H

// StateMachine.h
// A compact, table-driven C++ finite state machine (FSM) framework.
// 
// It uses a memory-efficient table-driven core while adding publisher/subscriber 
// signals and an optional asynchronous active-object mode using DelegateMQ.
// 
// Features:
// - Memory efficient (static state maps)
// - Support for guards, entry, and exit actions
// - Type-safe event data using std::shared_ptr
// - Built-in async active-object support via SetThread()
// - Pub/sub observation signals (OnTransition, OnEntry, OnExit, OnCannotHappen)
// - State machine inheritance support
// - Minimal heap/RAM footprint
// 
// @see https://github.com/DelegateMQ/active-fsm
// @author David Lafreniere (2000-2026)

#include <cstdint>
#include <typeinfo>
#include <memory>
#include "delegate-mq/DelegateMQ.h"
#include "delegate-mq/extras/util/Fault.h"

/// @brief Unique state machine event data must inherit from this class.
class EventData
{
public:
    virtual ~EventData() = default;
    XALLOCATOR
};

using NoEventData = EventData;

class StateMachine;

/// @brief Abstract state base class that all states inherit from.
class StateBase
{
public:
    /// Called by the state machine engine to execute a state action. If a guard condition
    /// exists and it evaluates to false, the state action will not execute. 
    /// @param[in] sm - A state machine instance. 
    /// @param[in] data - The event data. 
    virtual void InvokeStateAction(StateMachine* sm, std::shared_ptr<const EventData> data) const = 0;
};

/// @brief StateAction takes three template arguments: A state machine class,
/// a state function event data type (derived from EventData) and a state machine 
/// member function pointer.  
template <class SM, class Data, void (SM::*Func)(std::shared_ptr<const Data>)>
class StateAction : public StateBase
{
public:
    /// @see StateBase::InvokeStateAction
    virtual void InvokeStateAction(StateMachine* sm, std::shared_ptr<const EventData> data) const override
    {
        // Downcast the state machine and event data to the correct derived type
        SM* derivedSM = static_cast<SM*>(sm);
        
        std::shared_ptr<const Data> derivedData = std::static_pointer_cast<const Data>(data);

        // Call the state function
        (derivedSM->*Func)(derivedData);
    }
};

/// @brief Abstract guard base class that all guards classes inherit from.
class GuardBase
{
public:
    /// Called by the state machine engine to execute a guard condition action. If guard
    /// condition evaluates to true the state action is executed. If false, no state transition
    /// is performed.
    /// @param[in] sm - A state machine instance. 
    /// @param[in] data - The event data. 
    /// @return Returns true if no guard condition or the guard condition evaluates to true.
    virtual bool InvokeGuardCondition(StateMachine* sm, std::shared_ptr<const EventData> data) const = 0;
};

/// @brief GuardCondition takes three template arguments: A state machine class,
/// a state function event data type (derived from EventData) and a state machine 
/// member function pointer. 
template <class SM, class Data, bool (SM::*Func)(std::shared_ptr<const Data>)>
class GuardCondition : public GuardBase
{
public:
    virtual bool InvokeGuardCondition(StateMachine* sm, std::shared_ptr<const EventData> data) const override
    {
        SM* derivedSM = static_cast<SM*>(sm);		
        std::shared_ptr<const Data> derivedData = std::static_pointer_cast<const Data>(data);
        ASSERT_TRUE(derivedData != nullptr);

        // Call the guard function
        return (derivedSM->*Func)(derivedData);
    }
};

/// @brief Abstract entry base class that all entry classes inherit from.
class EntryBase
{
public:
    /// Called by the state machine engine to execute a state entry action. Called when
    /// entering a state. 
    /// @param[in] sm - A state machine instance. 
    /// @param[in] data - The event data.
    virtual void InvokeEntryAction(StateMachine* sm, std::shared_ptr<const EventData> data) const = 0;
};

/// @brief EntryAction takes three template arguments: A state machine class,
/// a state function event data type (derived from EventData) and a state machine 
/// member function pointer.  
template <class SM, class Data, void (SM::*Func)(std::shared_ptr<const Data>)>
class EntryAction : public EntryBase
{
public:
    virtual void InvokeEntryAction(StateMachine* sm, std::shared_ptr<const EventData> data) const override
    {
        SM* derivedSM = static_cast<SM*>(sm);
        std::shared_ptr<const Data> derivedData = std::static_pointer_cast<const Data>(data);
        ASSERT_TRUE(derivedData != nullptr);

        // Call the entry function
        (derivedSM->*Func)(derivedData);
    }
};

/// @brief Abstract exit base class that all exit classes inherit from.
class ExitBase
{
public:
    /// Called by the state machine engine to execute a state exit action. Called when
    /// leaving a state. 
    /// @param[in] sm - A state machine instance. 
    virtual void InvokeExitAction(StateMachine* sm) const = 0;
};

/// @brief ExitAction takes two template arguments: A state machine class and
/// a state machine member function pointer.   
template <class SM, void (SM::*Func)(void)>
class ExitAction : public ExitBase
{
public:
    virtual void InvokeExitAction(StateMachine* sm) const override
    {
        SM* derivedSM = static_cast<SM*>(sm);

        // Call the exit function
        (derivedSM->*Func)();
    }
};

/// @brief A structure to hold a single row within the state map. 
struct StateMapRow
{
    const StateBase* const State;
};

/// @brief A structure to hold a single row within the extended state map. 
struct StateMapRowEx
{
    const StateBase* const State;
    const GuardBase* const Guard;
    const EntryBase* const Entry;
    const ExitBase* const Exit;
};

/// @brief StateMachine implements a software-based state machine. 
class StateMachine 
{
public:
    enum { EVENT_IGNORED = 0xFE, CANNOT_HAPPEN = 0xFF };

    ///	Constructor.
    ///	@param[in] maxStates - the maximum number of state machine states.
    /// @param[in] initialState - the initial state machine state.
    StateMachine(uint8_t maxStates, uint8_t initialState = 0);

    virtual ~StateMachine() = default;

    /// Gets the current state machine state.
    /// @return Current state machine state.
    uint8_t GetCurrentState() const { return m_currentState; }

    /// Gets the maximum number of state machine states.
    /// @return The maximum state machine states. 
    uint8_t GetMaxStates() const { return MAX_STATES; }

    /// Enable active-object mode. ExternalEvent will marshal to @p thread.
    /// Call before the first ExternalEvent.
    virtual void SetThread(dmq::IThread& thread) { m_thread = &thread; }

    /// Fired after every completed state action: (fromState, toState).
    /// fromState == toState indicates a self-transition.
    dmq::Signal<void(uint8_t fromState, uint8_t toState)> OnTransition;

    /// Fired when entering a new state.
    dmq::Signal<void(uint8_t state)> OnEntry;

    /// Fired when exiting a state.
    dmq::Signal<void(uint8_t state)> OnExit;

    /// Fired when a CANNOT_HAPPEN transition is triggered.
    dmq::Signal<void(uint8_t state)> OnCannotHappen;
	
protected:
    /// External state machine event.
    /// @param[in] newState - the state machine state to transition to.
    /// @param[in] pData - the event data sent to the state.
    void ExternalEvent(uint8_t newState, std::shared_ptr<const EventData> pData = nullptr);

    /// Internal state machine event. These events are generated while executing
    ///	within a state machine state.
    /// @param[in] newState - the state machine state to transition to.
    /// @param[in] pData - the event data sent to the state.
    void InternalEvent(uint8_t newState, std::shared_ptr<const EventData> pData = nullptr);

    /// Helper function to determine if the current thread is the state machine thread.
    /// @return True if on the state machine thread or synchronous mode.
    bool IsOnStateMachineThread() const;

    /// Gets the state machine thread pointer.
    /// @return The thread pointer or nullptr if synchronous mode.
    dmq::IThread* GetThread() const { return m_thread; }

    /// Set a new current state.
    /// @param[in] newState - the new state.
    void SetCurrentState(uint8_t newState) { m_currentState = newState; }

    /// Accessors for subclass StateEngine overrides (e.g. StateMachineHSM).
    uint8_t GetNewState() const { return m_newState; }
    bool IsEventPending() const { return m_eventGenerated; }
    void ConsumeEvent(std::shared_ptr<const EventData>& outData) {
        outData = m_pEventData;
        m_pEventData = nullptr;
        m_eventGenerated = false;
    }

    /// State machine engine — virtual so subclasses (e.g. StateMachineHSM) can override.
    virtual void StateEngine();

private:
    /// The maximum number of state machine states.
    const uint8_t MAX_STATES;

    /// The current state machine state.
    uint8_t m_currentState;

    /// The new state the state machine has yet to transition to. 
    uint8_t m_newState;

    /// Set to true when an event is generated.
    bool m_eventGenerated;

    /// The state event data pointer.
    std::shared_ptr<const EventData> m_pEventData;

    /// Optional SM thread. Non-null enables active-object async dispatch.
    dmq::IThread* m_thread = nullptr;

    /// Gets the state map as defined in the derived class. The BEGIN_STATE_MAP,
    /// STATE_MAP_ENTRY and END_STATE_MAP macros are used to assist in creating the
    /// map. A state machine only needs to return a state map using either GetStateMap()  
    /// or GetStateMapEx() but not both. 
    /// @return An array of StateMapRow pointers with the array size MAX_STATES or
    /// NULL if the state machine uses the GetStateMapEx(). 
    virtual const StateMapRow* GetStateMap() = 0;

    /// Gets the extended state map as defined in the derived class. The BEGIN_STATE_MAP_EX,
    /// STATE_MAP_ENTRY_EX, STATE_MAP_ENTRY_ALL_EX, and END_STATE_MAP_EX macros are used to 
    /// assist in creating the map. A state machine only needs to return a state map using 
    /// either GetStateMap() or GetStateMapEx() but not both. 
    /// @return An array of StateMapRowEx pointers with the array size MAX_STATES or 
    /// NULL if the state machine uses the GetStateMap().
    virtual const StateMapRowEx* GetStateMapEx() = 0;

    /// ExternalEvent execution helper.
    void ExternalEventImpl(uint8_t newState, std::shared_ptr<const EventData> pData);

    void StateEngine(const StateMapRow* const pStateMap);
    void StateEngine(const StateMapRowEx* const pStateMapEx);
};

#define STATE_DECLARE(stateMachine, stateName, eventData) \
    void ST_##stateName(std::shared_ptr<const eventData>); \
    inline static StateAction<stateMachine, eventData, &stateMachine::ST_##stateName> stateName;
	
#define STATE_DEFINE(stateMachine, stateName, eventData) \
    void stateMachine::ST_##stateName(std::shared_ptr<const eventData> data)
		
#define GUARD_DECLARE(stateMachine, guardName, eventData) \
    bool GD_##guardName(std::shared_ptr<const eventData>); \
    inline static GuardCondition<stateMachine, eventData, &stateMachine::GD_##guardName> guardName;
	
#define GUARD_DEFINE(stateMachine, guardName, eventData) \
    bool stateMachine::GD_##guardName(std::shared_ptr<const eventData> data)

#define ENTRY_DECLARE(stateMachine, entryName, eventData) \
    void EN_##entryName(std::shared_ptr<const eventData>); \
    inline static EntryAction<stateMachine, eventData, &stateMachine::EN_##entryName> entryName;
	
#define ENTRY_DEFINE(stateMachine, entryName, eventData) \
    void stateMachine::EN_##entryName(std::shared_ptr<const eventData> data)

#define EXIT_DECLARE(stateMachine, exitName) \
    void EX_##exitName(void); \
    inline static ExitAction<stateMachine, &stateMachine::EX_##exitName> exitName;
	
#define EXIT_DEFINE(stateMachine, exitName) \
    void stateMachine::EX_##exitName(void)

#define BEGIN_TRANSITION_MAP(stateMachine, func, ...) \
    if (!IsOnStateMachineThread()) { \
        dmq::MakeDelegate(this, &stateMachine::func, *GetThread())(__VA_ARGS__); \
        return; \
    } \
    static const uint8_t TRANSITIONS[] = {\

#define TRANSITION_MAP_ENTRY(entry)\
    entry,

#define END_TRANSITION_MAP(data) \
    };\
    ASSERT_TRUE(GetCurrentState() < ST_MAX_STATES); \
    ExternalEvent(TRANSITIONS[GetCurrentState()], data); \
    static_assert((sizeof(TRANSITIONS)/sizeof(uint8_t)) == ST_MAX_STATES, "Transition map size mismatch"); 

#define PARENT_TRANSITION(state) \
    if (GetCurrentState() >= ST_MAX_STATES && \
        GetCurrentState() < GetMaxStates()) { \
        ExternalEvent(state); \
        return; }
	
#define BEGIN_STATE_MAP \
    private:\
    virtual const StateMapRowEx* GetStateMapEx() override { return nullptr; }\
    virtual const StateMapRow* GetStateMap() override {\
        static const StateMapRow STATE_MAP[] = { 

#define STATE_MAP_ENTRY(stateName)\
    stateName,

#define END_STATE_MAP \
    }; \
    static_assert((sizeof(STATE_MAP)/sizeof(StateMapRow)) == ST_MAX_STATES, "State map size mismatch"); \
    return &STATE_MAP[0]; }

#define BEGIN_STATE_MAP_EX \
    private:\
    virtual const StateMapRow* GetStateMap() override { return nullptr; }\
    virtual const StateMapRowEx* GetStateMapEx() override {\
        static const StateMapRowEx STATE_MAP[] = { 

#define STATE_MAP_ENTRY_EX(stateName)\
    { stateName, nullptr, nullptr, nullptr },

#define STATE_MAP_ENTRY_ALL_EX(stateName, guardName, entryName, exitName)\
    { stateName, guardName, entryName, exitName },

#define END_STATE_MAP_EX \
    }; \
    static_assert((sizeof(STATE_MAP)/sizeof(StateMapRowEx)) == ST_MAX_STATES, "State map size mismatch"); \
    return &STATE_MAP[0]; }

#endif // _STATE_MACHINE_H
