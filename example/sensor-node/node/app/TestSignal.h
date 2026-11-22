#ifndef SENSOR_NODE_TEST_SIGNAL_H
#define SENSOR_NODE_TEST_SIGNAL_H

/// @file TestSignal.h
/// @brief A generated fast signal, used by every platform until a real sensor
/// is wired: the desktop build, and the ESP32-S3 build without an ADC input.

#include "Hal.h"

#include <cmath>

namespace sensornode {

/// A 10 Hz sine of amplitude 1: each 100 ms window holds exactly one period,
/// so its mean is 0 and its min and max are close to -1 and 1 at any rate.
/// The PC client's self-test checks for that.
class SineSource : public ISignalSource {
public:
    static constexpr double FREQUENCY_HZ = 10.0;

    void Read(float* out, size_t n, uint32_t rateHz) override {
        constexpr double TWO_PI = 6.283185307179586;
        const double step = TWO_PI * FREQUENCY_HZ / rateHz;
        for (size_t i = 0; i < n; i++) {
            out[i] = static_cast<float>(std::sin(m_phase));
            m_phase += step;
            if (m_phase >= TWO_PI)
                m_phase -= TWO_PI;
        }
    }

private:
    double m_phase = 0.0;
};

} // namespace sensornode

#endif // SENSOR_NODE_TEST_SIGNAL_H
