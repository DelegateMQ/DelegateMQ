#ifndef PUMPTRON_F4_BOARD_H
#define PUMPTRON_F4_BOARD_H

#include "board/IBoard.h"
#include "stm32f4xx_hal.h"

namespace pumptron {
namespace board {

/// @brief IBoard backed by real STM32F4 Discovery (STM32F407G-DISC1) peripherals.
///
/// - Ambient temperature: MCU internal die temperature sensor (ADC1, polled).
/// - Vibration: LIS3DSH accelerometer over SPI1 (Discovery BSP). The reading is
///   the deviation of |acceleration| from a slowly tracking baseline, so the
///   board at rest reads ~0 g in any orientation and a shake or tap reads high.
/// - Local stop: blue USER button (PA0).
/// - Indicators: LD3 orange / LD4 green / LD5 red / LD6 blue.
///
/// All methods run on the pump controller thread, so the polled SPI/ADC HAL
/// calls need no locking.
class F4Board : public IBoard {
public:
    void Init() override;
    float ReadAmbientTempC() override;
    float ReadVibrationG() override;
    bool IsLocalStopPressed() override;
    void SetIndicator(Indicator indicator, bool on) override;
    void ToggleIndicator(Indicator indicator) override;
    const char* Name() const override { return "STM32F4 Discovery"; }

private:
    ADC_HandleTypeDef m_adc{};
    bool  m_accelOk = false;
    bool  m_adcOk = false;
    float m_ambientC = 25.0f;       ///< Low-pass filtered die temperature
    float m_accelBaselineMg = 1000.0f;
    float m_vibrationG = 0.0f;      ///< Peak-hold with decay
    bool  m_baselineSeeded = false;
};

} // namespace board
} // namespace pumptron

#endif
