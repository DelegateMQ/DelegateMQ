#ifndef _CONSTANTS_H
#define _CONSTANTS_H

#include "DelegateMQ.h"
#include "Logger.h"
#include <cstdint>

namespace cellutron {
    using namespace std::chrono_literals;

    // Centrifuge constants
    static constexpr uint16_t MAX_CENTRIFUGE_RPM = 1000;

    // Watchdog constants
    static constexpr dmq::Duration WATCHDOG_TIMEOUT = 30s;
    static constexpr dmq::Duration SYNC_INVOKE_TIMEOUT = 2s;

    // Timer constants
    static constexpr dmq::Duration TIMER_TICK_PERIOD = 10ms;

    // Heartbeat constants
    static constexpr dmq::Duration HEARTBEAT_PERIOD = 1s;
    static constexpr dmq::Duration HEARTBEAT_TIMEOUT = 10s;
    static constexpr dmq::Duration HEARTBEAT_WARMUP = 10s;

    // Thread priorities as portable bands (dmq::os::Thread::SetPriority()). Each
    // port maps a band to its own scale, so one set serves FreeRTOS (higher
    // number = more urgent) and ThreadX (lower number = more urgent) alike.
    // FreeRTOS, configMAX_PRIORITIES = 7: HIGHEST is 5, ABOVE_NORMAL 4,
    // BELOW_NORMAL 2, below the timer task at 6. ThreadX, 32 levels: 5, 10 and
    // 21, below the main app/watchdog threads at 0 (main_threadx.cpp).
    static constexpr dmq::os::ThreadPriority PRIORITY_NETWORK  = dmq::os::ThreadPriority::HIGHEST;
    static constexpr dmq::os::ThreadPriority PRIORITY_SYSTEM   = dmq::os::ThreadPriority::HIGHEST;
    static constexpr dmq::os::ThreadPriority PRIORITY_HARDWARE = dmq::os::ThreadPriority::ABOVE_NORMAL;
    static constexpr dmq::os::ThreadPriority PRIORITY_PROCESS  = dmq::os::ThreadPriority::ABOVE_NORMAL;
    static constexpr dmq::os::ThreadPriority PRIORITY_LOW      = dmq::os::ThreadPriority::BELOW_NORMAL;

    namespace topics {
        static const char* const SAFETY_HEARTBEAT     = "sys/heartbeat/safety";
        static const char* const CONTROLLER_HEARTBEAT = "sys/heartbeat/controller";
        static const char* const GUI_HEARTBEAT        = "sys/heartbeat/gui";

        static const char* const CMD_RUN              = "cell/cmd/run";
        static const char* const CMD_ABORT            = "cell/cmd/abort";
        static const char* const CMD_CENTRIFUGE_SPEED = "cell/cmd/centrifuge_speed";

        static const char* const STATUS_RUN           = "cell/status/run";
        static const char* const STATUS_CENTRIFUGE     = "cell/status/centrifuge";
        static const char* const STATUS_ACTUATOR      = "hw/status/actuator";
        static const char* const STATUS_SENSOR        = "hw/status/sensor";
        
        static const char* const FAULT                = "cell/fault";

        static const char* const PRESSURE_INLET       = "hw/sensor/pressure_inlet";
        static const char* const PRESSURE_OUTLET      = "hw/sensor/pressure_outlet";
        static const char* const AIR_INLET            = "hw/sensor/air_inlet";
        static const char* const AIR_OUTLET           = "hw/sensor/air_outlet";
        static const char* const RPM                  = "hw/sensor/rpm";
    }

    // Fixed-size container limits
    static constexpr size_t MAX_REMOTE_NODES       = 4;
    static constexpr size_t MAX_OUTGOING_TOPICS    = 12;
    static constexpr size_t MAX_INCOMING_TOPICS    = 12;
    static constexpr size_t MAX_HEARTBEAT_MONITORS = 4;

    // Fault codes
    enum FaultCode {
        FAULT_OVERSPEED = 1,
        FAULT_SAFETY_LOST = 2,
        FAULT_AIR_INLET = 3,
        FAULT_BLOCKAGE = 4,
        FAULT_CONTROLLER_LOST = 5,
        FAULT_GUI_LOST = 6,
        FAULT_HEARTBEAT_LOST = 7
    };

}

#endif
