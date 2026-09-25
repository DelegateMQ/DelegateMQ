#ifndef PUMPTRON_TOPOLOGY_H
#define PUMPTRON_TOPOLOGY_H

/// @file Topology.h
/// @brief The one place that defines which topics cross the link, in which
///        direction, and at which reliability tier.
///
/// Both functions are templates over the link type so the exact same wiring
/// applies to a `pumptron::SerialLink<...>` (real F4 board over RS-232) and a
/// `dmq::databus::NetworkNode<...>` (FreeRTOS simulator over UDP). Application
/// code on either side only ever calls DataBus::Publish/Subscribe and never
/// knows which link -- or whether any link -- is in use.

#include "Constants.h"
#include "Serializers.h"

namespace pumptron {

/// Controller side: publishes status/telemetry/alarms, receives commands.
template <typename Link>
void ConfigureControllerLink(Link& link)
{
    using Rel = dmq::databus::Reliability;

    link.template Receive<PumpCommandMsg>(topics::CMD,    RID_CMD,    serCommand);
    link.template Receive<HeartbeatMsg>  (topics::HB_GUI, RID_HB_GUI, serHeartbeat);

    // State changes, alarms: must arrive -> ACK + retry.
    link.template Send<PumpStatusMsg>(topics::STATUS,        RID_STATUS,        serStatus,    Rel::RELIABLE);
    link.template Send<AlarmMsg>     (topics::ALARM,         RID_ALARM,         serAlarm,     Rel::RELIABLE);
    link.template Send<CoreDumpMsg>  (topics::CORE_DUMP,     RID_CORE_DUMP,     serCoreDump,  Rel::RELIABLE);
    // High-rate or self-healing: newest value wins -> fire-and-forget.
    link.template Send<TelemetryMsg> (topics::TELEMETRY,     RID_TELEMETRY,     serTelemetry, Rel::UNRELIABLE);
    link.template Send<HeartbeatMsg> (topics::HB_CONTROLLER, RID_HB_CONTROLLER, serHeartbeat, Rel::UNRELIABLE);
}

/// GUI side: the mirror image of ConfigureControllerLink().
template <typename Link>
void ConfigureGuiLink(Link& link)
{
    using Rel = dmq::databus::Reliability;

    link.template Receive<PumpStatusMsg>(topics::STATUS,        RID_STATUS,        serStatus);
    link.template Receive<AlarmMsg>     (topics::ALARM,         RID_ALARM,         serAlarm);
    link.template Receive<TelemetryMsg> (topics::TELEMETRY,     RID_TELEMETRY,     serTelemetry);
    link.template Receive<HeartbeatMsg> (topics::HB_CONTROLLER, RID_HB_CONTROLLER, serHeartbeat);
    link.template Receive<CoreDumpMsg>  (topics::CORE_DUMP,     RID_CORE_DUMP,     serCoreDump);

    link.template Send<PumpCommandMsg>(topics::CMD,    RID_CMD,    serCommand,   Rel::RELIABLE);
    link.template Send<HeartbeatMsg>  (topics::HB_GUI, RID_HB_GUI, serHeartbeat, Rel::UNRELIABLE);
}

} // namespace pumptron

#endif
