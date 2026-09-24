#ifndef PUMPTRON_PUMP_MODEL_H
#define PUMPTRON_PUMP_MODEL_H

#include <cstdint>

namespace pumptron {
namespace pump {

/// @brief Simple centrifugal pump + motor physics model.
///
/// Deliberately small and deterministic apart from a little vibration noise:
/// - Speed follows the commanded target with a rate limit.
/// - Flow scales linearly with speed, discharge pressure with speed squared
///   (pump affinity laws).
/// - Motor temperature is a first-order lag toward a steady state that rises
///   with speed squared. Sustained full speed slowly overheats the motor --
///   an intentional demo scenario that ends in an OVER_TEMP trip.
/// - Vibration has a speed-proportional component plus a mild structural
///   resonance around RESONANCE_RPM, plus whatever the board senses externally.
///
/// Not thread-safe; owned and stepped by the PumpController thread.
class PumpModel {
public:
    /// Reset to a stopped, thermally-settled pump.
    void Reset(float ambientC);

    /// Advance the model.
    /// @param dtSec          Time step in seconds.
    /// @param targetRpm      Commanded speed.
    /// @param rampRpmPerSec  Maximum rate of speed change.
    /// @param ambientC       Ambient temperature.
    /// @param externalVibG   Externally sensed vibration (g).
    void Step(float dtSec, float targetRpm, float rampRpmPerSec, float ambientC, float externalVibG);

    float Rpm() const         { return m_rpm; }
    float FlowLpm() const     { return m_flowLpm; }
    float PressureBar() const { return m_pressureBar; }
    float MotorTempC() const  { return m_motorTempC; }
    float VibrationG() const  { return m_vibrationG; }

private:
    float Noise();  ///< Uniform noise in [-1, 1)

    float m_rpm = 0.0f;
    float m_flowLpm = 0.0f;
    float m_pressureBar = 0.0f;
    float m_motorTempC = 25.0f;
    float m_vibrationG = 0.0f;
    uint32_t m_rng = 0x2545F491u;
};

} // namespace pump
} // namespace pumptron

#endif
