#include "System.h"
#include "process/Process.h"
#include "actuators/Actuators.h"
#include "sensors/Sensors.h"
#include "Network.h"
#include "RemoteConfig.h"
#include "Constants.h"
#include "extras/util/ThreadMonitor.h"
#include <cstdio>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {

System::System()
    : m_thread("Controller_SystemThread", 200, FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "Controller")
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
    util::Network::GetInstance().Shutdown();
}

void System::Tick(uint32_t ms) {
    m_heartbeat.Tick(ms);
}

void System::SetupLocalSubscriptions() {
    m_startConn = dmq::databus::DataBus::Subscribe<StartProcessMsg>(topics::CMD_RUN, [this](StartProcessMsg msg) {
        if (m_startGuard.IsNewer(msg.seq)) {
            printf("Controller: >>>> RECEIVED START COMMAND <<<<\n");
            process::Process::GetInstance().Start();
        }
    }, &m_thread);

    m_stopConn = dmq::databus::DataBus::Subscribe<StopProcessMsg>(topics::CMD_ABORT, [this](StopProcessMsg msg) {
        if (m_stopGuard.IsNewer(msg.seq)) {
            printf("Controller: >>>> RECEIVED ABORT COMMAND <<<<\n");
            process::Process::GetInstance().Abort();
        }
    }, &m_thread);

    m_faultConn = dmq::databus::DataBus::Subscribe<FaultMsg>(topics::FAULT, [this](FaultMsg msg) {
        // NOTE: IsNewer() guard intentionally omitted for faults. Prioritize safety
        // over ordering; a "nuisance trip" from an old fault is safer than missing 
        // a trip due to a sequencing race.
        if (process::Process::GetInstance().GetCellProcess().GetCurrentState() != process::CellProcess::ST_FAULT) {
            printf("Controller: >>>> CRITICAL FAULT RECEIVED (Code: %d) <<<<\n", msg.faultCode);
            process::Process::GetInstance().Fault();
        }
    }, &m_thread);
}

void System::SetupNetwork() {
    util::Network::GetInstance().Initialize(5011, "Controller", "Controller"); 
    
    // Incoming from Network
    util::Network::GetInstance().RegisterIncomingTopic<StartProcessMsg>(topics::CMD_RUN, RID_START_PROCESS, serStart);
    util::Network::GetInstance().RegisterIncomingTopic<StopProcessMsg>(topics::CMD_ABORT, RID_STOP_PROCESS, serStop);
    util::Network::GetInstance().RegisterIncomingTopic<FaultMsg>(topics::FAULT, RID_FAULT_EVENT, serFault);
    util::Network::GetInstance().RegisterIncomingTopic<HeartbeatMsg>(topics::SAFETY_HEARTBEAT, RID_SAFETY_HB, serHeartbeat);
    util::Network::GetInstance().RegisterIncomingTopic<HeartbeatMsg>(topics::GUI_HEARTBEAT, RID_GUI_HB, serHeartbeat);

    // Setup Outgoing Topics
    util::Network::GetInstance().AddRemoteNode("GUI", "127.0.0.1", 5010);
    util::Network::GetInstance().AddRemoteNode("Safety", "127.0.0.1", 5013);

    util::Network::GetInstance().RegisterOutgoingTopic<RunStatusMsg>(topics::STATUS_RUN, RID_RUN_STATUS, serRun, util::Network::Reliability::RELIABLE);
    util::Network::GetInstance().RegisterOutgoingTopic<FaultMsg>(topics::FAULT, RID_FAULT_EVENT, serFault, util::Network::Reliability::RELIABLE);
    util::Network::GetInstance().RegisterOutgoingTopic<HeartbeatMsg>(topics::CONTROLLER_HEARTBEAT, RID_CONTROLLER_HB, serHeartbeat);
    util::Network::GetInstance().RegisterOutgoingTopic<CentrifugeSpeedMsg>(topics::CMD_CENTRIFUGE_SPEED, RID_CENTRIFUGE_SPEED, serSpeed);
    util::Network::GetInstance().RegisterOutgoingTopic<ActuatorStatusMsg>(topics::STATUS_ACTUATOR, RID_ACTUATOR_STATUS, serActuator);
    util::Network::GetInstance().RegisterOutgoingTopic<SensorStatusMsg>(topics::STATUS_SENSOR, RID_SENSOR_STATUS, serSensor);
    util::Network::GetInstance().RegisterOutgoingTopic<SensorStatusMsg>(topics::AIR_INLET, RID_SENSOR_STATUS, serSensor);
    util::Network::GetInstance().RegisterOutgoingTopic<SensorStatusMsg>(topics::AIR_OUTLET, RID_SENSOR_STATUS, serSensor);
    util::Network::GetInstance().RegisterOutgoingTopic<SensorStatusMsg>(topics::PRESSURE_INLET, RID_SENSOR_STATUS, serSensor);
    util::Network::GetInstance().RegisterOutgoingTopic<SensorStatusMsg>(topics::PRESSURE_OUTLET, RID_SENSOR_STATUS, serSensor);
    util::Network::GetInstance().RegisterOutgoingTopic<CentrifugeSpeedMsg>(topics::RPM, RID_CENTRIFUGE_STATUS, serSpeed);
}

void System::SetupWatchdog() {
    m_heartbeat.MonitorNode(topics::SAFETY_HEARTBEAT, FAULT_SAFETY_LOST, "Safety");
    m_heartbeat.MonitorNode(topics::GUI_HEARTBEAT, FAULT_GUI_LOST, "GUI");
}

} // namespace cellutron
