#include "PumpModel.h"
#include <cmath>

namespace pumptron {
namespace pump {

namespace {
    constexpr float FLOW_LPM_PER_RPM     = 0.02f;   // 3000 RPM -> 60 L/min
    constexpr float PRESSURE_AT_MAX_BAR  = 4.0f;
    constexpr float RATED_RPM            = 3000.0f;

    // Steady-state motor temperature rise = HEAT_COEFF * rpm^2 above ambient.
    // 2500 RPM settles ~69 C (below warning), 2700 RPM ~76 C (warning),
    // 3000 RPM ~88 C (trips OVER_TEMP after roughly a minute).
    constexpr float HEAT_COEFF           = 7.0e-6f;
    constexpr float THERMAL_TAU_SEC      = 20.0f;

    constexpr float VIB_BASE_G           = 0.02f;
    constexpr float VIB_PER_RATED_G      = 0.05f;
    constexpr float VIB_NOISE_G          = 0.015f;
    constexpr float RESONANCE_RPM        = 2200.0f;
    constexpr float RESONANCE_WIDTH_RPM  = 120.0f;
    constexpr float RESONANCE_PEAK_G     = 0.20f;
}

void PumpModel::Reset(float ambientC)
{
    m_rpm = 0.0f;
    m_flowLpm = 0.0f;
    m_pressureBar = 0.0f;
    m_motorTempC = ambientC;
    m_vibrationG = 0.0f;
}

float PumpModel::Noise()
{
    // xorshift32 -- cheap, deterministic, no libc rand() state shared across threads
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0xFFFFu) / 32768.0f - 1.0f;
}

void PumpModel::Step(float dtSec, float targetRpm, float rampRpmPerSec, float ambientC, float externalVibG)
{
    // Rate-limited speed
    const float maxDelta = rampRpmPerSec * dtSec;
    const float delta = targetRpm - m_rpm;
    if (delta > maxDelta)       m_rpm += maxDelta;
    else if (delta < -maxDelta) m_rpm -= maxDelta;
    else                        m_rpm = targetRpm;
    if (m_rpm < 0.0f) m_rpm = 0.0f;

    // Affinity laws
    const float speedRatio = m_rpm / RATED_RPM;
    m_flowLpm = m_rpm * FLOW_LPM_PER_RPM;
    m_pressureBar = PRESSURE_AT_MAX_BAR * speedRatio * speedRatio;

    // First-order thermal lag toward steady state
    const float steadyC = ambientC + HEAT_COEFF * m_rpm * m_rpm;
    m_motorTempC += (steadyC - m_motorTempC) * (dtSec / THERMAL_TAU_SEC);

    // Vibration: only meaningful while turning, plus external input always
    float vib = 0.0f;
    if (m_rpm > 1.0f) {
        const float d = (m_rpm - RESONANCE_RPM) / RESONANCE_WIDTH_RPM;
        vib = VIB_BASE_G
            + VIB_PER_RATED_G * speedRatio
            + RESONANCE_PEAK_G * std::exp(-0.5f * d * d)
            + VIB_NOISE_G * Noise();
    }
    m_vibrationG = std::fabs(vib) + externalVibG;
}

} // namespace pump
} // namespace pumptron
