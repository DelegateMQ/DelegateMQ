#ifndef SENSOR_NODE_STD_FINE_CLOCK_H
#define SENSOR_NODE_STD_FINE_CLOCK_H

/// @file StdFineClock.h
/// @brief IFineClock from std::chrono::steady_clock, for platforms where the
/// C++ library's clock is the real, fine-grained one: the desktop, and RTOS
/// simulators running as host processes.

#include "Hal.h"

#include <chrono>

namespace sensornode {

class StdFineClock : public IFineClock {
public:
    uint64_t NowNs() override {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
};

} // namespace sensornode

#endif // SENSOR_NODE_STD_FINE_CLOCK_H
