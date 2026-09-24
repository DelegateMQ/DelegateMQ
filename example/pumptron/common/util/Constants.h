#ifndef PUMPTRON_CONSTANTS_H
#define PUMPTRON_CONSTANTS_H

#include "DelegateMQ.h"
#include <cstdint>

namespace pumptron {
    using namespace std::chrono_literals;

    // -----------------------------------------------------------------------
    // Pump operating envelope
    // -----------------------------------------------------------------------
    static constexpr uint16_t MIN_SETPOINT_RPM     = 500;
    static constexpr uint16_t MAX_SETPOINT_RPM     = 3000;
    static constexpr uint16_t DEFAULT_SETPOINT_RPM = 1500;
    static constexpr uint16_t SETPOINT_STEP_RPM    = 100;
    static constexpr uint16_t PRIME_RPM            = 400;
    static constexpr float    STOPPED_RPM          = 5.0f;   ///< Below this the pump counts as stopped

    static constexpr float    RAMP_UP_RPM_PER_S    = 600.0f; ///< Normal acceleration
    static constexpr float    RAMP_DOWN_RPM_PER_S  = 800.0f; ///< Controlled STOP deceleration
    static constexpr float    COAST_RPM_PER_S      = 2500.0f;///< E-STOP / fault coast-down

    static constexpr dmq::Duration PRIME_TIME      = 3s;

    // Motor temperature thresholds (deg C)
    static constexpr float TEMP_WARN_C  = 70.0f;
    static constexpr float TEMP_TRIP_C  = 85.0f;
    static constexpr float TEMP_RESET_C = 60.0f;   ///< RESET refused until cooled below this

    // Vibration thresholds (g, deviation from rest)
    static constexpr float VIB_WARN_G   = 0.35f;
    static constexpr float VIB_TRIP_G   = 0.80f;
    static constexpr dmq::Duration VIB_TRIP_TIME = 300ms;  ///< Sustained time above VIB_TRIP_G to trip

    // -----------------------------------------------------------------------
    // Timing
    // -----------------------------------------------------------------------
    static constexpr dmq::Duration CONTROL_PERIOD    = 50ms;   ///< Pump model/control loop tick
    static constexpr uint32_t      TELEMETRY_DIVIDER = 2;      ///< Telemetry every N control ticks (10 Hz)
    static constexpr dmq::Duration HEARTBEAT_PERIOD  = 500ms;
    static constexpr dmq::Duration HEARTBEAT_TIMEOUT = 3s;
    static constexpr dmq::Duration WATCHDOG_TIMEOUT  = 10s;
    static constexpr dmq::Duration TIMER_TICK_PERIOD = 10ms;   ///< Timer::ProcessTimers() cadence
    static constexpr dmq::Duration LINK_DEGRADED_HOLD = 10s;   ///< LINK_DEGRADED clears after this long error-free
    static constexpr dmq::Duration RESYNC_MIN_INTERVAL = 1s;   ///< Rate limit for status/alarm republish on delivery failure

    // -----------------------------------------------------------------------
    // Links
    // -----------------------------------------------------------------------
    static constexpr int      SERIAL_BAUD          = 115200;
    static constexpr uint16_t GUI_UDP_PORT         = 5020;     ///< Sim mode: GUI listens here
    static constexpr uint16_t CONTROLLER_UDP_PORT  = 5021;     ///< Sim mode: controller listens here

    // -----------------------------------------------------------------------
    // DataBus topics
    // -----------------------------------------------------------------------
    namespace topics {
        static const char* const CMD            = "pump/cmd";
        static const char* const STATUS         = "pump/status";
        static const char* const TELEMETRY      = "pump/telemetry";
        static const char* const ALARM          = "pump/alarm";
        static const char* const HB_CONTROLLER  = "sys/heartbeat/controller";
        static const char* const HB_GUI         = "sys/heartbeat/gui";
    }

    // -----------------------------------------------------------------------
    // Remote IDs (wire identifiers, one per topic)
    // -----------------------------------------------------------------------
    static constexpr dmq::DelegateRemoteId RID_CMD           = 200;
    static constexpr dmq::DelegateRemoteId RID_STATUS        = 201;
    static constexpr dmq::DelegateRemoteId RID_TELEMETRY     = 202;
    static constexpr dmq::DelegateRemoteId RID_ALARM         = 203;
    static constexpr dmq::DelegateRemoteId RID_HB_CONTROLLER = 204;
    static constexpr dmq::DelegateRemoteId RID_HB_GUI        = 205;

    // -----------------------------------------------------------------------
    // Controller thread priorities (FreeRTOS: higher number = more urgent).
    // configMAX_PRIORITIES = 7; the timer daemon and watchdog task sit at 6.
    // -----------------------------------------------------------------------
    static constexpr int PRIORITY_LINK   = 5;
    static constexpr int PRIORITY_PUMP   = 4;
    static constexpr int PRIORITY_SYSTEM = 3;

} // namespace pumptron

#endif
