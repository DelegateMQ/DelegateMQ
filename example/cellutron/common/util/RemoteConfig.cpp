#include "RemoteConfig.h"
#include "util/Constants.h"

namespace cellutron {

dmq::serialization::serializer::Serializer<void(StartProcessMsg)>    serStart;
dmq::serialization::serializer::Serializer<void(StopProcessMsg)>     serStop;
dmq::serialization::serializer::Serializer<void(CentrifugeSpeedMsg)> serSpeed;
dmq::serialization::serializer::Serializer<void(CentrifugeStatusMsg)> serStatus;
dmq::serialization::serializer::Serializer<void(RunStatusMsg)>       serRun;
dmq::serialization::serializer::Serializer<void(FaultMsg)>           serFault;
dmq::serialization::serializer::Serializer<void(ActuatorStatusMsg)>  serActuator;
dmq::serialization::serializer::Serializer<void(SensorStatusMsg)>    serSensor;
dmq::serialization::serializer::Serializer<void(HeartbeatMsg)>       serHeartbeat;

void RegisterSerializers() {
    dmq::databus::DataBus::RegisterSerializer<StartProcessMsg>(cellutron::topics::CMD_RUN, serStart);
    dmq::databus::DataBus::RegisterSerializer<StopProcessMsg>(cellutron::topics::CMD_ABORT, serStop);
    dmq::databus::DataBus::RegisterSerializer<CentrifugeSpeedMsg>(cellutron::topics::CMD_CENTRIFUGE_SPEED, serSpeed);
    dmq::databus::DataBus::RegisterSerializer<CentrifugeStatusMsg>(cellutron::topics::STATUS_CENTRIFUGE, serStatus);
    dmq::databus::DataBus::RegisterSerializer<RunStatusMsg>(cellutron::topics::STATUS_RUN, serRun);
    dmq::databus::DataBus::RegisterSerializer<FaultMsg>(cellutron::topics::FAULT, serFault);
    dmq::databus::DataBus::RegisterSerializer<ActuatorStatusMsg>(cellutron::topics::STATUS_ACTUATOR, serActuator);
    dmq::databus::DataBus::RegisterSerializer<SensorStatusMsg>(cellutron::topics::STATUS_SENSOR, serSensor);
    dmq::databus::DataBus::RegisterSerializer<HeartbeatMsg>(cellutron::topics::SAFETY_HEARTBEAT, serHeartbeat);
    dmq::databus::DataBus::RegisterSerializer<HeartbeatMsg>(cellutron::topics::CONTROLLER_HEARTBEAT, serHeartbeat);
    dmq::databus::DataBus::RegisterSerializer<HeartbeatMsg>(cellutron::topics::GUI_HEARTBEAT, serHeartbeat);
}

void RegisterStringifiers() {
    dmq::databus::DataBus::RegisterStringifier<StartProcessMsg>(cellutron::topics::CMD_RUN, [](const StartProcessMsg&) {
        return "START";
    });

    dmq::databus::DataBus::RegisterStringifier<StopProcessMsg>(cellutron::topics::CMD_ABORT, [](const StopProcessMsg&) {
        return "ABORT";
    });

    dmq::databus::DataBus::RegisterStringifier<CentrifugeSpeedMsg>(cellutron::topics::CMD_CENTRIFUGE_SPEED, [](const CentrifugeSpeedMsg& msg) {
        return std::to_string(msg.rpm) + " RPM";
    });

    dmq::databus::DataBus::RegisterStringifier<CentrifugeStatusMsg>(cellutron::topics::STATUS_CENTRIFUGE, [](const CentrifugeStatusMsg& msg) {
        return std::to_string(msg.rpm) + " RPM";
    });

    dmq::databus::DataBus::RegisterStringifier<RunStatusMsg>(cellutron::topics::STATUS_RUN, [](const RunStatusMsg& msg) {
        switch(msg.status) {
            case RunStatus::IDLE: return "IDLE";
            case RunStatus::PROCESSING: return "PROCESSING";
            case RunStatus::ABORTING: return "ABORTING";
            case RunStatus::FAULT: return "FAULT";
            default: return "UNKNOWN";
        }
    });

    dmq::databus::DataBus::RegisterStringifier<FaultMsg>(cellutron::topics::FAULT, [](const FaultMsg& msg) {
        return "FAULT CODE: " + std::to_string(msg.faultCode);
    });

    dmq::databus::DataBus::RegisterStringifier<ActuatorStatusMsg>(cellutron::topics::STATUS_ACTUATOR, [](const ActuatorStatusMsg& msg) {
        std::string type = (msg.type == ActuatorType::VALVE) ? "VALVE" : "PUMP";
        return type + " " + std::to_string(msg.id) + ": " + std::to_string(msg.value);
    });

    dmq::databus::DataBus::RegisterStringifier<SensorStatusMsg>(cellutron::topics::STATUS_SENSOR, [](const SensorStatusMsg& msg) {
        std::string type = (msg.type == SensorType::PRESSURE) ? "PRESSURE" : "AIR";
        return type + ": " + std::to_string(msg.value);
    });

    dmq::databus::DataBus::RegisterStringifier<HeartbeatMsg>(cellutron::topics::SAFETY_HEARTBEAT, [](const HeartbeatMsg& msg) {
        return "SAFETY HB: " + std::to_string(msg.counter);
    });

    dmq::databus::DataBus::RegisterStringifier<HeartbeatMsg>(cellutron::topics::CONTROLLER_HEARTBEAT, [](const HeartbeatMsg& msg) {
        return "CONTROLLER HB: " + std::to_string(msg.counter);
    });

    dmq::databus::DataBus::RegisterStringifier<HeartbeatMsg>(cellutron::topics::GUI_HEARTBEAT, [](const HeartbeatMsg& msg) {
        return "GUI HB: " + std::to_string(msg.counter);
    });

    dmq::databus::DataBus::RegisterStringifier<SensorStatusMsg>(cellutron::topics::PRESSURE_INLET, [](const SensorStatusMsg& msg) {
        return "Inlet: " + std::to_string(msg.value) + " mmHg";
    });

    dmq::databus::DataBus::RegisterStringifier<SensorStatusMsg>(cellutron::topics::PRESSURE_OUTLET, [](const SensorStatusMsg& msg) {
        return "Outlet: " + std::to_string(msg.value) + " mmHg";
    });

    dmq::databus::DataBus::RegisterStringifier<SensorStatusMsg>(cellutron::topics::AIR_INLET, [](const SensorStatusMsg& msg) {
        return (msg.value == 1) ? "AIR DETECTED" : "FLUID OK";
    });

    dmq::databus::DataBus::RegisterStringifier<SensorStatusMsg>(cellutron::topics::AIR_OUTLET, [](const SensorStatusMsg& msg) {
        return (msg.value == 1) ? "AIR DETECTED" : "FLUID OK";
    });

    dmq::databus::DataBus::RegisterStringifier<CentrifugeSpeedMsg>(cellutron::topics::RPM, [](const CentrifugeSpeedMsg& msg) {
        return "RPM: " + std::to_string(msg.rpm);
    });
}

} // namespace cellutron
