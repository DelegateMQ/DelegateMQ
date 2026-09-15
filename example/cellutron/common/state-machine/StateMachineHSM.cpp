#include "StateMachineHSM.h"

//----------------------------------------------------------------------------
// StateMachineHSM
//----------------------------------------------------------------------------
StateMachineHSM::StateMachineHSM(uint8_t maxStates, uint8_t initialState)
    : StateMachine(maxStates, initialState)
{
    // Sentinel values 0xFC and 0xFD must not collide with valid state indices.
    DMQ_ASSERT_TRUE(maxStates < PROPAGATE_TO_PARENT);
}

//----------------------------------------------------------------------------
// StateEngine
//----------------------------------------------------------------------------
void StateMachineHSM::StateEngine()
{
    const StateMapRowHSM* map = GetStateMapHSM();
    DMQ_ASSERT_TRUE(map != nullptr);

    std::shared_ptr<const EventData> pDataTemp;

    while (IsEventPending())
    {
        uint8_t newState = GetNewState();
        DMQ_ASSERT_TRUE(newState < GetMaxStates());

        const StateBase* state = map[newState].State;
        const GuardBase* guard = map[newState].Guard;

        ConsumeEvent(pDataTemp);

        bool guardResult = true;
        if (guard != nullptr)
            guardResult = guard->InvokeGuardCondition(this, pDataTemp);

        if (guardResult)
        {
            uint8_t fromState = GetCurrentState();

            if (newState != fromState)
            {
                uint8_t lca = FindLCA(fromState, newState, map);

                // Exit from current state up to (not including) LCA.
                uint8_t s = fromState;
                while (s < GetMaxStates() && s != lca)
                {
                    if (map[s].Exit != nullptr)
                        map[s].Exit->InvokeExitAction(this);
                    OnExit(s);
                    s = map[s].ParentState;
                }

                // Collect entry chain: target up to (not including) LCA.
                // Stored leaf-first; executed in reverse (root-first).
                static const uint8_t MAX_HSM_DEPTH = 32;
                uint8_t entryChain[MAX_HSM_DEPTH];
                uint8_t entryCount = 0;
                s = newState;
                while (s < GetMaxStates() && s != lca && entryCount < MAX_HSM_DEPTH)
                {
                    entryChain[entryCount++] = s;
                    s = map[s].ParentState;
                }

                SetCurrentState(newState);

                // Execute entries parent-first (reverse of collection order).
                for (int i = (int)entryCount - 1; i >= 0; i--)
                {
                    if (map[entryChain[i]].Entry != nullptr)
                        map[entryChain[i]].Entry->InvokeEntryAction(this, pDataTemp);
                    OnEntry(entryChain[i]);
                }

                // Entry/exit actions must not fire new events.
                DMQ_ASSERT_TRUE(!IsEventPending());
            }
            else
            {
                SetCurrentState(newState);
            }

            DMQ_ASSERT_TRUE(state != nullptr);
            state->InvokeStateAction(this, pDataTemp);

            OnTransition(fromState, GetCurrentState());
        }
    }
}

//----------------------------------------------------------------------------
// FindLCA
// O(depth^2) walk — depth is typically 2–5 levels, so this is fine.
//----------------------------------------------------------------------------
uint8_t StateMachineHSM::FindLCA(uint8_t stateA, uint8_t stateB,
                                  const StateMapRowHSM* map) const
{
    uint8_t maxStates = GetMaxStates();

    // For each ancestor of stateA (including itself), check whether it is also
    // an ancestor of stateB (including stateB itself).
    uint8_t a = stateA;
    while (a < maxStates)
    {
        uint8_t b = stateB;
        while (b < maxStates)
        {
            if (a == b)
                return a;
            b = map[b].ParentState;
        }
        a = map[a].ParentState;
    }

    return static_cast<uint8_t>(NO_PARENT);
}
