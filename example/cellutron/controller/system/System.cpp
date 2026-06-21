#include "System.h"
#include "Logger.h"
#include "process/Process.h"
#include "actuators/Actuators.h"
#include "sensors/Sensors.h"
#include "RemoteConfig.h"
#include "Constants.h"
#include "extras/util/ThreadMonitor.h"
#include "SpyBridge.h"
#include "NodeBridge.h"
#include <cstdio>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {

System::System()
    : m_thread("Controller_SystemThread", 50, FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "Controller")
    , m_heartbeat("Controller", topics::CONTROLLER_HEARTBEAT, m_thread)
{
}

void System::Initialize() {
    printf("Controller: System initializing...\n");

    cellutron::RegisterSerializers();
    cellutron::RegisterStringifiers();

    // Register thread for monitoring
    ThreadMonitor::Register(&m_thread);
    ThreadMonitor::Enable();

    // 1. Create System Thread
#ifndef DMQ_THREAD_STDLIB
    m_thread.SetThreadPriority(PRIORITY_SYSTEM);
#endif
    if (!m_thread.CreateThread(WATCHDOG_TIMEOUT)) {
        printf("Controller: ERROR - Failed to create system thread!\n");
        return;
    }

    // 2. Instantiate state machines
    process::Process::GetInstance().Initialize();

    // 3. Initialize Subsystems
    actuators::Actuators::GetInstance().Initialize();
    sensors::Sensors::GetInstance().Initialize();

    // 4. Setup Wiring
    dmq::databus::DataBus::LastValueCache(topics::STATUS_RUN, true);
    SetupLocalSubscriptions();
    SetupNetwork();
    SetupWatchdog();

    // 5. Start Heartbeat
    m_heartbeat.Start();

    printf("Controller: System ready.\n");
}

void System::Shutdown() {
    m_thread.ExitThread();
    m_network.Stop();
}

void System::Tick(uint32_t ms) {
    m_heartbeat.Tick(ms);
}

void System::SetupLocalSubscriptions() {
    m_startConn = dmq::databus::DataBus::Subscribe<StartProcessMsg>(topics::CMD_RUN, dmq::MakeDelegate(this, &System::OnStart), &m_thread);
    m_stopConn = dmq::databus::DataBus::Subscribe<StopProcessMsg>(topics::CMD_ABORT, dmq::MakeDelegate(this, &System::OnStop), &m_thread);
    m_faultConn = dmq::databus::DataBus::Subscribe<FaultMsg>(topics::FAULT, dmq::MakeDelegate(this, &System::OnFault), &m_thread);
}

void System::OnStart(StartProcessMsg msg) {
    if (m_startGuard.IsNewer(msg.seq)) {
        printf("Controller: >>>> RECEIVED START COMMAND <<<<\n");
        process::Process::GetInstance().Start();
    }
}

void System::OnStop(StopProcessMsg msg) {
    if (m_stopGuard.IsNewer(msg.seq)) {
        printf("Controller: >>>> RECEIVED ABORT COMMAND <<<<\n");
        process::Process::GetInstance().Abort();
    }
}

void System::OnFault(FaultMsg msg) {
    // NOTE: IsNewer() guard intentionally omitted for faults. Prioritize safety
    // over ordering; a "nuisance trip" from an old fault is safer than missing 
    // a trip due to a sequencing race.
    if (process::Process::GetInstance().GetCellProcess().GetCurrentState() != process::CellProcess::ST_FAULT) {
        printf("Controller: >>>> CRITICAL FAULT RECEIVED (Code: %d) <<<<\n", msg.faultCode);
        process::Process::GetInstance().Fault();
    }
}

void System::SetupNetwork() {
    SpyBridge::Start("127.0.0.1", 9999, "Controller");
    NodeBridge::StartMulticast("Controller", "239.1.1.1", 9998);

    m_network.Start("Controller", /*listenPort=*/5011);

    // Incoming Topics
    m_network.Receive<StartProcessMsg>(topics::CMD_RUN,            RID_START_PROCESS,   serStart);
    m_network.Receive<StopProcessMsg> (topics::CMD_ABORT,          RID_STOP_PROCESS,    serStop);
    m_network.Receive<FaultMsg>       (topics::FAULT,              RID_FAULT_EVENT,     serFault);
    m_network.Receive<HeartbeatMsg>   (topics::SAFETY_HEARTBEAT,   RID_SAFETY_HB,       serHeartbeat);
    m_network.Receive<HeartbeatMsg>   (topics::GUI_HEARTBEAT,      RID_GUI_HB,          serHeartbeat);

    // Remote peers
    m_network.AddPeer("GUI",    "127.0.0.1", /*udpPort=*/5010);
    m_network.AddPeer("Safety", "127.0.0.1", /*udpPort=*/5013);

    // Outgoing Topics — critical messages use RELIABLE (ACK + retry); telemetry uses UNRELIABLE
    using Rel = dmq::databus::Reliability;
    m_network.Send<RunStatusMsg>     (topics::STATUS_RUN,           RID_RUN_STATUS,       serRun,      Rel::RELIABLE);
    m_network.Send<FaultMsg>         (topics::FAULT,                RID_FAULT_EVENT,      serFault,    Rel::RELIABLE);
    m_network.Send<HeartbeatMsg>     (topics::CONTROLLER_HEARTBEAT, RID_CONTROLLER_HB,    serHeartbeat);
    m_network.Send<CentrifugeSpeedMsg>(topics::CMD_CENTRIFUGE_SPEED, RID_CENTRIFUGE_SPEED, serSpeed);
    m_network.Send<ActuatorStatusMsg>(topics::STATUS_ACTUATOR,      RID_ACTUATOR_STATUS,  serActuator);
    m_network.Send<SensorStatusMsg>  (topics::STATUS_SENSOR,        RID_SENSOR_STATUS,    serSensor);
    m_network.Send<SensorStatusMsg>  (topics::AIR_INLET,            RID_SENSOR_STATUS,    serSensor);
    m_network.Send<SensorStatusMsg>  (topics::AIR_OUTLET,           RID_SENSOR_STATUS,    serSensor);
    m_network.Send<SensorStatusMsg>  (topics::PRESSURE_INLET,       RID_SENSOR_STATUS,    serSensor);
    m_network.Send<SensorStatusMsg>  (topics::PRESSURE_OUTLET,      RID_SENSOR_STATUS,    serSensor);
    m_network.Send<CentrifugeSpeedMsg>(topics::RPM,                 RID_CENTRIFUGE_STATUS, serSpeed);
}

void System::SetupWatchdog() {
    m_heartbeat.MonitorNode(topics::SAFETY_HEARTBEAT, FAULT_SAFETY_LOST, "Safety");
    m_heartbeat.MonitorNode(topics::GUI_HEARTBEAT, FAULT_GUI_LOST, "GUI");
}

} // namespace cellutron
