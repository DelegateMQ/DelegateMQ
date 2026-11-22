#include "NodeMain.h"

#include <chrono>
#include <cstdio>
#include <memory>

using namespace dmq;
using namespace dmq::util;

namespace sensornode {

namespace {
    constexpr auto LOOP_PERIOD = std::chrono::milliseconds(1);
    constexpr auto WATCHDOG_PERIOD = std::chrono::milliseconds(100);
    constexpr auto SUMMARY_PERIOD = std::chrono::seconds(10);

    void PrintCounters(const char* prefix, const NodeCounters& c)
    {
        printf("%s%u stats windows, %u temperatures, %u touches published; %u commands; %u dropped by NetThread\n",
               prefix, static_cast<unsigned>(c.statsPublished), static_cast<unsigned>(c.temperaturesPublished),
               static_cast<unsigned>(c.touchesPublished), static_cast<unsigned>(c.commands),
               static_cast<unsigned>(c.netDropped));
    }
}

bool RunNode(Platform& platform, const NodeConfig& config, Duration runFor, const std::atomic<bool>* stop)
{
    // On the heap: its socket buffers would crowd a small RTOS main-task stack
    auto node = std::make_unique<SensorNode>(platform);
    node->Start(config);
    if (config.onStarted)
        config.onStarted(*node);

    // The platform's part of the timing: DelegateMQ timers, IsrDelegate posts
    // (the touch interrupt) and the thread watchdogs
    const TimePoint start = Timer::GetNow();
    TimePoint lastWatchdog = start;
    TimePoint lastSummary = start;
    for (;;) {
        Timer::ProcessTimers();
        IsrDelegates::ProcessAll();

        const TimePoint now = Timer::GetNow();
        if (now - lastWatchdog >= WATCHDOG_PERIOD) {
            os::Thread::WatchdogCheckAll();
            lastWatchdog = now;
        }
        if (now - lastSummary >= SUMMARY_PERIOD) {
            PrintCounters("Node: ", node->GetCounters());
            lastSummary = now;
        }
        if ((stop && stop->load()) || (runFor != Duration::zero() && now - start >= runFor))
            break;

        ThisThread::sleep_for(LOOP_PERIOD);
    }

    if (config.onStopping)
        config.onStopping(*node);
    node->Stop();

    // The node's own checks; the PC client checks the data and the commands
    const NodeCounters c = node->GetCounters();
    PrintCounters("Sensor node stopped: ", c);
    const bool ok = c.statsPublished > 0 && c.temperaturesPublished > 0 && c.netDropped == 0;
    printf("%s\n", ok ? "NODE CHECKS PASSED" : "NODE CHECKS FAILED");
    return ok;
}

} // namespace sensornode
