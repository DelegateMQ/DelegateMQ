#ifndef PUMPTRON_SERIALIZERS_H
#define PUMPTRON_SERIALIZERS_H

#include "DelegateMQ.h"
#include "messages/PumpCommandMsg.h"
#include "messages/PumpStatusMsg.h"
#include "messages/TelemetryMsg.h"
#include "messages/AlarmMsg.h"
#include "messages/HeartbeatMsg.h"
#include "messages/CoreDumpMsg.h"

namespace pumptron {

// Shared serializer instances. One per message type; referenced by both the
// link wiring (Topology.h) and DataBus::RegisterSerializer.
extern dmq::serialization::serializer::Serializer<void(PumpCommandMsg)> serCommand;
extern dmq::serialization::serializer::Serializer<void(PumpStatusMsg)>  serStatus;
extern dmq::serialization::serializer::Serializer<void(TelemetryMsg)>   serTelemetry;
extern dmq::serialization::serializer::Serializer<void(AlarmMsg)>       serAlarm;
extern dmq::serialization::serializer::Serializer<void(HeartbeatMsg)>   serHeartbeat;
extern dmq::serialization::serializer::Serializer<void(CoreDumpMsg)>    serCoreDump;

} // namespace pumptron

#endif
