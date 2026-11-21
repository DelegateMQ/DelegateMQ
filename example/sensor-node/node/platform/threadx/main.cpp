/// @file main.cpp
/// @brief The sensor node on ThreadX: ThreadX's Linux port (a Linux process
/// running the real ThreadX kernel), with the desktop stand-ins for the
/// hardware and host UDP sockets. The program itself is RunNode(), the same as
/// on the desktop and the ESP32-S3; only this file differs.
///
///   sensor_node [seconds] [pc-address]
///
/// seconds: run time; 0 (the default) runs until killed.
/// pc-address: where to publish (default 127.0.0.1). Run sensor_pc there.
///
/// Single core: no core pinning (NodeConfig's masks stay 0). ThreadX's default
/// tick is 10 ms, so RunNode()'s 1 ms loop sleep is one tick.

#include "NodeMain.h"
#include "DesktopHal.h"
#include "StdFineClock.h"
#include "TestSignal.h"

#include "tx_api.h"

#include <cstdio>
#include <cstdlib>

using namespace sensornode;

namespace {

/// The touch "interrupt": a ThreadX timer, which runs in ThreadX's timer
/// thread, touches and releases pad 0 every 700 ms through the IsrDelegate,
/// like the desktop's stand-in thread and the board's GPIO interrupt.
class ThreadXTouch : public ITouchInput {
public:
    void Start(Sink& sink) override {
        m_sink = &sink;
        s_instance = this;      // not through the timer's ULONG: 32 bits on x86-64 Linux
        const ULONG ticks = 700 * TX_TIMER_TICKS_PER_SECOND / 1000;
        tx_timer_create(&m_timer, const_cast<CHAR*>("Touch"), &ThreadXTouch::OnTimer,
                        0, ticks, ticks, TX_AUTO_ACTIVATE);
    }
    void Stop() override {
        tx_timer_deactivate(&m_timer);
        tx_timer_delete(&m_timer);
    }

private:
    static void OnTimer(ULONG) {
        ThreadXTouch* self = s_instance;
        self->m_sink->Post(0, self->m_touched);
        self->m_touched = self->m_touched ? 0 : 1;
    }

    static inline ThreadXTouch* s_instance = nullptr;
    TX_TIMER m_timer{};
    Sink* m_sink = nullptr;
    uint8_t m_touched = 1;      // timer thread only
};

int s_seconds = 0;
NodeConfig s_config;

constexpr ULONG NODE_STACK_SIZE = 65536;
TX_THREAD s_nodeThread;
UCHAR s_nodeStack[NODE_STACK_SIZE];

void NodeThreadEntry(ULONG)
{
    SineSource signal;
    desktop::DriftingTemperature temperature;
    desktop::ConsoleLed led;
    ThreadXTouch touch;
    StdFineClock clock;
    Platform platform{ signal, temperature, led, touch, clock };

    const bool ok = RunNode(platform, s_config, std::chrono::seconds(s_seconds));
    fflush(stdout);
    std::exit(ok ? 0 : 1);     // ends the process, kernel and all
}

} // namespace

// ThreadX calls this once from tx_kernel_enter(), before scheduling starts
extern "C" void tx_application_define(void*)
{
    tx_thread_create(&s_nodeThread, const_cast<CHAR*>("NodeMain"), NodeThreadEntry, 0,
                     s_nodeStack, NODE_STACK_SIZE, 0, 0, TX_NO_TIME_SLICE, TX_AUTO_START);
}

int main(int argc, char* argv[])
{
    s_seconds = argc > 1 ? std::atoi(argv[1]) : 0;
    if (argc > 2)
        s_config.pcAddress = argv[2];
    printf("Sensor node on ThreadX (Linux port)%s\n", s_seconds > 0 ? "" : ". Kill it to stop.");
    tx_kernel_enter();      // never returns
    return 0;
}
