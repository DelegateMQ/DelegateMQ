#ifndef SENSOR_NODE_DESKTOP_HAL_H
#define SENSOR_NODE_DESKTOP_HAL_H

/// @file DesktopHal.h
/// @brief Desktop stand-ins for the ESP32-S3's peripherals, so the whole node
/// runs and is tested on Windows and Linux.

#include "Hal.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace sensornode::desktop {

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

/// Around 24 C, drifting slowly
class DriftingTemperature : public ITemperatureSensor {
public:
    float ReadCelsius() override {
        const double s = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        return static_cast<float>(24.0 + 0.5 * std::sin(s / 30.0));
    }
};

class ConsoleLed : public ILed {
public:
    void Set(uint8_t r, uint8_t g, uint8_t b) override {
        printf("[LED] r=%u g=%u b=%u\n", r, g, b);
    }
};

/// Stands in for the touch pad interrupt: a thread that touches and releases
/// pad 0 every 700 ms, posting through the IsrDelegate like the interrupt does
class TouchSimulator : public ITouchInput {
public:
    ~TouchSimulator() override { Stop(); }

    void Start(Sink& sink) override {
        m_running = true;
        m_thread = std::thread([this, &sink] {
            uint8_t touched = 1;
            while (m_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                sink.Post(0, touched);
                touched = touched ? 0 : 1;
            }
        });
    }

    void Stop() override {
        m_running = false;
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace sensornode::desktop

#endif // SENSOR_NODE_DESKTOP_HAL_H
