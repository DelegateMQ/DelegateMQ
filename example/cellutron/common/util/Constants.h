#ifndef _CONSTANTS_H
#define _CONSTANTS_H

#include "DelegateMQ.h"
#include <cstdint>

namespace cellutron {
    using namespace std::chrono_literals;

    // Centrifuge constants
    static constexpr uint16_t MAX_CENTRIFUGE_RPM = 1000;

    // Watchdog constants
    static constexpr dmq::Duration WATCHDOG_TIMEOUT = 5s;
    static constexpr dmq::Duration SYNC_INVOKE_TIMEOUT = 2s;

    // Heartbeat constants
    static constexpr dmq::Duration HEARTBEAT_PERIOD = 500ms;
    static constexpr dmq::Duration HEARTBEAT_TIMEOUT = 5s;
    static constexpr dmq::Duration HEARTBEAT_WARMUP = 10s;

    // Thread Priorities (FreeRTOS levels - range 0 to configMAX_PRIORITIES-1)
    // Note: configMAX_PRIORITIES=7. Timer Task is 6. We stay <= 5.
    static constexpr int PRIORITY_NETWORK   = 5;
    static constexpr int PRIORITY_HARDWARE  = 4;
    static constexpr int PRIORITY_PROCESS   = 4;
    static constexpr int PRIORITY_SYSTEM    = 5;
    static constexpr int PRIORITY_LOW       = 2;

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
