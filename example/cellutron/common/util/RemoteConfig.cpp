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

static dmq::xstring StringifyStart(const StartProcessMsg&) { return "START"; }
static dmq::xstring StringifyStop(const StopProcessMsg&) { return "ABORT"; }

static dmq::xstring StringifyCentrifugeSpeed(const CentrifugeSpeedMsg& msg) {
    dmq::xostringstream oss;
    oss << msg.rpm << " RPM";
    return oss.str();
}

static dmq::xstring StringifyCentrifugeStatus(const CentrifugeStatusMsg& msg) {
    dmq::xostringstream oss;
    oss << msg.rpm << " RPM";
    return oss.str();
}

static dmq::xstring StringifyRunStatus(const RunStatusMsg& msg) {
    switch(msg.status) {
        case RunStatus::IDLE: return "IDLE";
        case RunStatus::PROCESSING: return "PROCESSING";
        case RunStatus::ABORTING: return "ABORTING";
        case RunStatus::FAULT: return "FAULT";
        default: return "UNKNOWN";
    }
}

static dmq::xstring StringifyFault(const FaultMsg& msg) {
    dmq::xostringstream oss;
    oss << "FAULT CODE: " << msg.faultCode;
    return oss.str();
}

static dmq::xstring StringifyActuatorStatus(const ActuatorStatusMsg& msg) {
    dmq::xostringstream oss;
    oss << ((msg.type == ActuatorType::VALVE) ? "VALVE" : "PUMP") << " " << msg.id << ": " << msg.value;
    return oss.str();
}

static dmq::xstring StringifySensorStatus(const SensorStatusMsg& msg) {
    dmq::xostringstream oss;
    oss << ((msg.type == SensorType::PRESSURE) ? "PRESSURE" : "AIR") << ": " << msg.value;
    return oss.str();
}

static dmq::xstring StringifyHeartbeat(const HeartbeatMsg& msg) {
    dmq::xostringstream oss;
    oss << msg.counter;
    return oss.str();
}

static dmq::xstring StringifyInletPressure(const SensorStatusMsg& msg) {
    dmq::xostringstream oss;
    oss << "Inlet: " << msg.value << " mmHg";
    return oss.str();
}

static dmq::xstring StringifyOutletPressure(const SensorStatusMsg& msg) {
    dmq::xostringstream oss;
    oss << "Outlet: " << msg.value << " mmHg";
    return oss.str();
}

static dmq::xstring StringifyRPM(const CentrifugeSpeedMsg& msg) {
    dmq::xostringstream oss;
    oss << "RPM: " << msg.rpm;
    return oss.str();
}

static dmq::xstring StringifyAirInlet(const SensorStatusMsg& msg) {
    return (msg.value == 1) ? "AIR DETECTED" : "FLUID OK";
}

static dmq::xstring StringifyAirOutlet(const SensorStatusMsg& msg) {
    return (msg.value == 1) ? "AIR DETECTED" : "FLUID OK";
}

void RegisterStringifiers() {
    dmq::databus::DataBus::RegisterStringifier<StartProcessMsg>(cellutron::topics::CMD_RUN, dmq::MakeDelegate(&StringifyStart));
    dmq::databus::DataBus::RegisterStringifier<StopProcessMsg>(cellutron::topics::CMD_ABORT, dmq::MakeDelegate(&StringifyStop));
    dmq::databus::DataBus::RegisterStringifier<CentrifugeSpeedMsg>(cellutron::topics::CMD_CENTRIFUGE_SPEED, dmq::MakeDelegate(&StringifyCentrifugeSpeed));
    dmq::databus::DataBus::RegisterStringifier<CentrifugeStatusMsg>(cellutron::topics::STATUS_CENTRIFUGE, dmq::MakeDelegate(&StringifyCentrifugeStatus));
    dmq::databus::DataBus::RegisterStringifier<RunStatusMsg>(cellutron::topics::STATUS_RUN, dmq::MakeDelegate(&StringifyRunStatus));
    dmq::databus::DataBus::RegisterStringifier<FaultMsg>(cellutron::topics::FAULT, dmq::MakeDelegate(&StringifyFault));
    dmq::databus::DataBus::RegisterStringifier<ActuatorStatusMsg>(cellutron::topics::STATUS_ACTUATOR, dmq::MakeDelegate(&StringifyActuatorStatus));
    dmq::databus::DataBus::RegisterStringifier<SensorStatusMsg>(cellutron::topics::STATUS_SENSOR, dmq::MakeDelegate(&StringifySensorStatus));
    dmq::databus::DataBus::RegisterStringifier<HeartbeatMsg>(cellutron::topics::SAFETY_HEARTBEAT, dmq::MakeDelegate(&StringifyHeartbeat));
    dmq::databus::DataBus::RegisterStringifier<HeartbeatMsg>(cellutron::topics::CONTROLLER_HEARTBEAT, dmq::MakeDelegate(&StringifyHeartbeat));
    dmq::databus::DataBus::RegisterStringifier<HeartbeatMsg>(cellutron::topics::GUI_HEARTBEAT, dmq::MakeDelegate(&StringifyHeartbeat));
    dmq::databus::DataBus::RegisterStringifier<SensorStatusMsg>(cellutron::topics::PRESSURE_INLET, dmq::MakeDelegate(&StringifyInletPressure));
    dmq::databus::DataBus::RegisterStringifier<SensorStatusMsg>(cellutron::topics::PRESSURE_OUTLET, dmq::MakeDelegate(&StringifyOutletPressure));
    dmq::databus::DataBus::RegisterStringifier<SensorStatusMsg>(cellutron::topics::AIR_INLET, dmq::MakeDelegate(&StringifyAirInlet));
    dmq::databus::DataBus::RegisterStringifier<SensorStatusMsg>(cellutron::topics::AIR_OUTLET, dmq::MakeDelegate(&StringifyAirOutlet));
    dmq::databus::DataBus::RegisterStringifier<CentrifugeSpeedMsg>(cellutron::topics::RPM, dmq::MakeDelegate(&StringifyRPM));
}

} // namespace cellutron
