#ifndef PUMPTRON_SIM_BOARD_H
#define PUMPTRON_SIM_BOARD_H

#include "board/IBoard.h"
#include <cstdio>

namespace pumptron {
namespace board {

/// @brief IBoard for the FreeRTOS simulator. No real peripherals: ambient
///        temperature is fixed, there is no external vibration input and no
///        stop button. Indicator changes are printed to the console.
class SimBoard : public IBoard {
public:
    void Init() override {}

    float ReadAmbientTempC() override { return 25.0f; }
    float ReadVibrationG() override { return 0.0f; }
    bool IsLocalStopPressed() override { return false; }

    void SetIndicator(Indicator indicator, bool on) override {
        const int i = static_cast<int>(indicator);
        if (m_state[i] == on) return;
        m_state[i] = on;
        if (indicator != Indicator::ACTIVITY)
            printf("SimBoard: LED %s %s\n", Name(indicator), on ? "ON" : "off");
    }

    void ToggleIndicator(Indicator indicator) override {
        const int i = static_cast<int>(indicator);
        m_state[i] = !m_state[i];
    }

    const char* Name() const override { return "Simulator"; }

private:
    static const char* Name(Indicator indicator) {
        switch (indicator) {
            case Indicator::POWER:    return "POWER";
            case Indicator::RUNNING:  return "RUNNING";
            case Indicator::FAULT:    return "FAULT";
            case Indicator::ACTIVITY: return "ACTIVITY";
        }
        return "?";
    }

    bool m_state[4] = {};
};

} // namespace board
} // namespace pumptron

#endif
