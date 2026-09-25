#include "Serializers.h"

namespace pumptron {

dmq::serialization::serializer::Serializer<void(PumpCommandMsg)> serCommand;
dmq::serialization::serializer::Serializer<void(PumpStatusMsg)>  serStatus;
dmq::serialization::serializer::Serializer<void(TelemetryMsg)>   serTelemetry;
dmq::serialization::serializer::Serializer<void(AlarmMsg)>       serAlarm;
dmq::serialization::serializer::Serializer<void(HeartbeatMsg)>   serHeartbeat;
dmq::serialization::serializer::Serializer<void(CoreDumpMsg)>    serCoreDump;

} // namespace pumptron
