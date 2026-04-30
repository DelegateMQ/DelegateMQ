#ifndef _STATE_MACHINE_HSM_H
#define _STATE_MACHINE_HSM_H

// StateMachineHSM.h
// Extends StateMachine with hierarchical state machine (HSM) support.
//
// Each state may declare a parent state. On transition, exit actions run from
// the current state up to (but not including) the Least Common Ancestor (LCA),
// then entry actions run from the LCA down to the target state.
//
// PROPAGATE_TO_PARENT in a transition map causes the event lookup to retry
// against the parent state's transition table entry, walking up the hierarchy
// until a state handles it or the top is reached (treated as EVENT_IGNORED).
//
// @see https://github.com/DelegateMQ/active-fsm
// @author David Lafreniere (2000-2026)

#include "StateMachine.h"

/// @brief Extended state map row for HSM. Adds a ParentState field.
struct StateMapRowHSM
{
    const StateBase* const State;
    const GuardBase* const Guard;
    const EntryBase* const Entry;
    const ExitBase* const Exit;
    uint8_t ParentState;  ///< Parent state index, or NO_PARENT for top-level states.
};

/// @brief StateMachineHSM adds hierarchical state support on top of StateMachine.
///
/// Usage:
///   1. Inherit from StateMachineHSM instead of StateMachine.
///   2. Define states with STATE_DECLARE / STATE_DEFINE as usual.
///   3. Use BEGIN_STATE_MAP_HSM / END_STATE_MAP_HSM with STATE_MAP_ENTRY_HSM or
///      STATE_MAP_ENTRY_ALL_HSM, specifying the parent state (or NO_PARENT).
///   4. In external event methods, use BEGIN_TRANSITION_MAP (unchanged) with
///      END_TRANSITION_MAP_HSM instead of END_TRANSITION_MAP. Entries that
///      should propagate to the parent use PROPAGATE_TO_PARENT.
class StateMachineHSM : public StateMachine
{
public:
    enum { NO_PARENT = 0xFD, PROPAGATE_TO_PARENT = 0xFC };

    StateMachineHSM(uint8_t maxStates, uint8_t initialState = 0);
    virtual ~StateMachineHSM() = default;

protected:
    /// Overrides the flat StateEngine with an LCA-aware hierarchical engine.
    virtual void StateEngine() override;

    virtual const StateMapRowHSM* GetStateMapHSM() = 0;

private:
    virtual const StateMapRow*   GetStateMap()   override final { return nullptr; }
    virtual const StateMapRowEx* GetStateMapEx() override final { return nullptr; }

    /// Returns the Least Common Ancestor of stateA and stateB, or NO_PARENT.
    uint8_t FindLCA(uint8_t stateA, uint8_t stateB, const StateMapRowHSM* map) const;
};

// ---------------------------------------------------------------------------
// HSM state map macros
// ---------------------------------------------------------------------------

#define BEGIN_STATE_MAP_HSM \
    private: \
    virtual const StateMapRowHSM* GetStateMapHSM() override { \
        static const StateMapRowHSM STATE_MAP[] = {

/// Entry with state action only; no guard, entry, or exit.
#define STATE_MAP_ENTRY_HSM(stateName, parentState) \
    { stateName, nullptr, nullptr, nullptr, parentState },

/// Entry with all four action slots.
#define STATE_MAP_ENTRY_ALL_HSM(stateName, guardName, entryName, exitName, parentState) \
    { stateName, guardName, entryName, exitName, parentState },

#define END_STATE_MAP_HSM \
    }; \
    static_assert((sizeof(STATE_MAP) / sizeof(StateMapRowHSM)) == ST_MAX_STATES, \
        "HSM state map size mismatch"); \
    return &STATE_MAP[0]; }

// ---------------------------------------------------------------------------
// HSM transition map end macro
// Replaces END_TRANSITION_MAP when PROPAGATE_TO_PARENT entries are needed.
// Resolves propagation before calling ExternalEvent so the engine never
// receives PROPAGATE_TO_PARENT as a raw target state.
// ---------------------------------------------------------------------------

#define END_TRANSITION_MAP_HSM(data) \
    }; \
    ASSERT_TRUE(GetCurrentState() < ST_MAX_STATES); \
    { \
        const StateMapRowHSM* _map = GetStateMapHSM(); \
        uint8_t _state   = GetCurrentState(); \
        uint8_t _resolved = TRANSITIONS[_state]; \
        while (_resolved == PROPAGATE_TO_PARENT) { \
            uint8_t _parent = _map[_state].ParentState; \
            if (_parent >= ST_MAX_STATES) { _resolved = EVENT_IGNORED; break; } \
            _state    = _parent; \
            _resolved = TRANSITIONS[_state]; \
        } \
        ExternalEvent(_resolved, data); \
    } \
    static_assert((sizeof(TRANSITIONS) / sizeof(uint8_t)) == ST_MAX_STATES, \
        "Transition map size mismatch");

#endif // _STATE_MACHINE_HSM_H
