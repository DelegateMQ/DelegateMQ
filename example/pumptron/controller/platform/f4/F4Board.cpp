#include "F4Board.h"
#include "stm32f4_discovery.h"
#include "stm32f4_discovery_accelerometer.h"
#include <cmath>
#include <cstdio>

namespace pumptron {
namespace board {

namespace {
    // STM32F407 datasheet: V25 = 0.76 V, Avg_Slope = 2.5 mV/C.
    // The Discovery board runs VDD/VREF+ at 3.0 V.
    constexpr float VREF_V          = 3.0f;
    constexpr float TS_V25          = 0.76f;
    constexpr float TS_SLOPE_V_PER_C = 0.0025f;

    // Filter time constants (seconds). Weights are derived from the real
    // elapsed time each Sample(), so behaviour doesn't depend on the control
    // tick rate or its jitter.
    constexpr float TEMP_TAU_S      = 1.0f;    // die temperature smoothing
    constexpr float BASELINE_TAU_S  = 5.0f;    // slow tracking of |a| at rest
    constexpr float VIB_DECAY_TAU_S = 0.3f;    // peak-hold decay: a shake stays visible
                                               // across the 300 ms VIB_TRIP_TIME window

    /// Exponential-smoothing weight for a first-order filter with time
    /// constant tau after dt seconds: 1 - e^(-dt/tau).
    float Alpha(float dtSec, float tauSec) {
        return dtSec <= 0.0f ? 0.0f : 1.0f - std::exp(-dtSec / tauSec);
    }

    Led_TypeDef ToLed(Indicator indicator) {
        switch (indicator) {
            case Indicator::POWER:    return LED3;   // orange
            case Indicator::RUNNING:  return LED4;   // green
            case Indicator::FAULT:    return LED5;   // red
            case Indicator::ACTIVITY: return LED6;   // blue
        }
        return LED3;
    }
}

void F4Board::Init()
{
    BSP_LED_Init(LED3);
    BSP_LED_Init(LED4);
    BSP_LED_Init(LED5);
    BSP_LED_Init(LED6);
    BSP_PB_Init(BUTTON_KEY, BUTTON_MODE_GPIO);

    m_accelOk = (BSP_ACCELERO_Init() == ACCELERO_OK);
    if (!m_accelOk)
        printf("F4Board: WARNING - accelerometer init failed; vibration input disabled\n");

    // ADC1 single conversion of the internal temperature sensor.
    __HAL_RCC_ADC1_CLK_ENABLE();
    m_adc.Instance = ADC1;
    m_adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    m_adc.Init.Resolution = ADC_RESOLUTION_12B;
    m_adc.Init.ScanConvMode = DISABLE;
    m_adc.Init.ContinuousConvMode = DISABLE;
    m_adc.Init.DiscontinuousConvMode = DISABLE;
    m_adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    m_adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    m_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    m_adc.Init.NbrOfConversion = 1;
    m_adc.Init.DMAContinuousRequests = DISABLE;
    m_adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;

    ADC_ChannelConfTypeDef channel{};
    channel.Channel = ADC_CHANNEL_TEMPSENSOR;
    channel.Rank = 1;
    channel.SamplingTime = ADC_SAMPLETIME_480CYCLES;   // sensor needs >= 10 us

    m_adcOk = HAL_ADC_Init(&m_adc) == HAL_OK && HAL_ADC_ConfigChannel(&m_adc, &channel) == HAL_OK;
    if (!m_adcOk)
        printf("F4Board: WARNING - ADC init failed; ambient temperature fixed at 25 C\n");
    else
        m_ambientC = -1000.0f;   // seed from first sample
}

void F4Board::Sample(float dtSec)
{
    // Polled ADC conversion and a 6-byte SPI read: tens of microseconds in
    // total, negligible against the 50 ms control period. Move to DMA or a
    // separate task only if the loop ever needs tighter timing.
    if (m_adcOk) {
        HAL_ADC_Start(&m_adc);
        if (HAL_ADC_PollForConversion(&m_adc, 2) == HAL_OK) {
            const float volts = static_cast<float>(HAL_ADC_GetValue(&m_adc)) * VREF_V / 4095.0f;
            const float c = (volts - TS_V25) / TS_SLOPE_V_PER_C + 25.0f;
            m_ambientC = (m_ambientC < -500.0f) ? c : m_ambientC + Alpha(dtSec, TEMP_TAU_S) * (c - m_ambientC);
        }
        HAL_ADC_Stop(&m_adc);
    }

    if (m_accelOk) {
        int16_t xyz[3] = { 0, 0, 0 };   // milli-g
        BSP_ACCELERO_GetXYZ(xyz);
        const float x = xyz[0], y = xyz[1], z = xyz[2];
        const float magnitudeMg = std::sqrt(x * x + y * y + z * z);

        if (!m_baselineSeeded) {
            m_accelBaselineMg = magnitudeMg;
            m_baselineSeeded = true;
        }
        const float deviationG = std::fabs(magnitudeMg - m_accelBaselineMg) / 1000.0f;
        m_accelBaselineMg += Alpha(dtSec, BASELINE_TAU_S) * (magnitudeMg - m_accelBaselineMg);

        // Peak-hold so a shake stays visible across several control ticks.
        const float decay = dtSec <= 0.0f ? 1.0f : std::exp(-dtSec / VIB_DECAY_TAU_S);
        m_vibrationG = std::fmax(deviationG, m_vibrationG * decay);
    }
}

float F4Board::ReadAmbientTempC()
{
    return m_ambientC;
}

float F4Board::ReadVibrationG()
{
    return m_accelOk ? m_vibrationG : 0.0f;
}

bool F4Board::IsLocalStopPressed()
{
    return BSP_PB_GetState(BUTTON_KEY) != 0;
}

void F4Board::SetIndicator(Indicator indicator, bool on)
{
    if (on) BSP_LED_On(ToLed(indicator));
    else    BSP_LED_Off(ToLed(indicator));
}

void F4Board::ToggleIndicator(Indicator indicator)
{
    BSP_LED_Toggle(ToLed(indicator));
}

} // namespace board
} // namespace pumptron
