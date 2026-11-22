/// @file main.cpp
/// @brief The sensor node on Zephyr's native_sim board (a Linux process
/// running the real Zephyr kernel), with stand-ins for the hardware and
/// Zephyr's sockets offloaded to the host. The program itself is RunNode(),
/// the same as on the desktop, ThreadX and the ESP32-S3; only this file differs.
///
///   zephyr.exe -testargs [seconds] [pc-address]
///
/// seconds: run time; 0 (the default) runs until killed.
/// pc-address: where to publish (default 127.0.0.1). Run sensor_pc there.
///
/// native_sim is single core: no core pinning (NodeConfig's masks stay 0).

#include "NodeMain.h"
#include "TestSignal.h"

#include <zephyr/kernel.h>
#include "cmdline.h"        // native_sim: native_get_test_cmd_line_args()

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace sensornode;

namespace {

/// Around 24 C, drifting slowly (like the desktop's stand-in)
class DriftingTemperature : public ITemperatureSensor {
public:
    float ReadCelsius() override {
        const double s = static_cast<double>(k_uptime_get()) / 1000.0;
        return static_cast<float>(24.0 + 0.5 * std::sin(s / 30.0));
    }
};

class ConsoleLed : public ILed {
public:
    void Set(uint8_t r, uint8_t g, uint8_t b) override {
        printf("[LED] r=%u g=%u b=%u\n", r, g, b);
    }
};

/// The touch "interrupt": a k_timer, whose expiry function runs in interrupt
/// context, touches and releases pad 0 every 700 ms through the IsrDelegate,
/// like the board's GPIO interrupt
class TimerTouch : public ITouchInput {
public:
    void Start(Sink& sink) override {
        m_sink = &sink;
        k_timer_init(&m_timer, &TimerTouch::OnTimer, nullptr);
        k_timer_user_data_set(&m_timer, this);
        k_timer_start(&m_timer, K_MSEC(700), K_MSEC(700));
    }
    void Stop() override {
        k_timer_stop(&m_timer);
    }

private:
    static void OnTimer(struct k_timer* timer) {
        auto* self = static_cast<TimerTouch*>(k_timer_user_data_get(timer));
        self->m_sink->Post(0, self->m_touched);
        self->m_touched = self->m_touched ? 0 : 1;
    }

    struct k_timer m_timer{};
    Sink* m_sink = nullptr;
    uint8_t m_touched = 1;      // timer expiry only
};

/// The kernel's cycle counter, in nanoseconds. On native_sim, simulated time
/// advances only when the CPU idles, so it can't time code that runs:
/// MeasuresRunTime() is false there (true on a real target).
class CycleClock : public IFineClock {
public:
    uint64_t NowNs() override { return k_cyc_to_ns_floor64(k_cycle_get_64()); }
#if defined(CONFIG_BOARD_NATIVE_SIM)
    bool MeasuresRunTime() const override { return false; }
#endif
};

} // namespace

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    int argc = 0;
    char** argv = nullptr;
    native_get_test_cmd_line_args(&argc, &argv);
    const int seconds = argc > 0 ? std::atoi(argv[0]) : 0;
    NodeConfig config;
    if (argc > 1)
        config.pcAddress = argv[1];
    printf("Sensor node on Zephyr (native_sim)%s\n", seconds > 0 ? "" : ". Kill it to stop.");

    SineSource signal;
    DriftingTemperature temperature;
    ConsoleLed led;
    TimerTouch touch;
    CycleClock clock;
    Platform platform{ signal, temperature, led, touch, clock };

    const bool ok = RunNode(platform, config, std::chrono::seconds(seconds));
    std::exit(ok ? 0 : 1);
}
