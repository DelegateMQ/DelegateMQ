#include "System.h"
#include "Network.h"
#include "ui/UI.h"
#include "logs/Logs.h"
#include "alarms/Alarms.h"
#include "RemoteConfig.h"
#include "Constants.h"
#include "extras/util/ThreadMonitor.h"
#include "extras/util/ThreadMonitorSer.h"
#include <cstdio>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {

System::System()
    : m_thread("GUI_SystemThread", 100, FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "GUI")
    , m_backgroundTimer("GUI_TimerThread", 0, FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "GUI")
    , m_heartbeat("GUI", topics::GUI_HEARTBEAT, m_thread)
{
}

System::~System() {
    Shutdown();
}

void System::Initialize() {
    printf("GUI: System initializing...\n");

    cellutron::RegisterSerializers();
    cellutron::RegisterStringifiers();
    
    // Register threads for monitoring
    ThreadMonitor::Register(&m_thread);
    ThreadMonitor::Register(&m_backgroundTimer);
    ThreadMonitor::Enable();

    m_thread.CreateThread(WATCHDOG_TIMEOUT);

    SetupNetwork();
    SetupWatchdog();
    
    util::Logs::GetInstance().Initialize();
    util::Alarms::GetInstance().Initialize();

    StartTimerThread();

    m_heartbeat.Start();

    printf("GUI: System ready.\n");
}

void System::Shutdown() {
    if (m_timerRunning) {
        m_timerRunning = false;
        m_backgroundTimer.ExitThread();
        m_thread.ExitThread();

        util::Alarms::GetInstance().Shutdown();
        util::Logs::GetInstance().Shutdown();
        util::Network::GetInstance().Shutdown();
    }
}

void System::Tick(uint32_t ms) {
    m_heartbeat.Tick(ms);
}

void System::SetupNetwork() {
    util::Network::GetInstance().Initialize(5010, "GUI", "GUI"); 
    // Incoming Topics
    util::Network::GetInstance().RegisterIncomingTopic<RunStatusMsg>(topics::STATUS_RUN, RID_RUN_STATUS, serRun);
    util::Network::GetInstance().RegisterIncomingTopic<CentrifugeSpeedMsg>(topics::CMD_CENTRIFUGE_SPEED, RID_CENTRIFUGE_SPEED, serSpeed);
    util::Network::GetInstance().RegisterIncomingTopic<CentrifugeSpeedMsg>(topics::RPM, RID_CENTRIFUGE_STATUS, serSpeed);
    util::Network::GetInstance().RegisterIncomingTopic<FaultMsg>(topics::FAULT, RID_FAULT_EVENT, serFault);
    util::Network::GetInstance().RegisterIncomingTopic<ActuatorStatusMsg>(topics::STATUS_ACTUATOR, RID_ACTUATOR_STATUS, serActuator);
    util::Network::GetInstance().RegisterIncomingTopic<SensorStatusMsg>(topics::STATUS_SENSOR, RID_SENSOR_STATUS, serSensor);
    util::Network::GetInstance().RegisterIncomingTopic<HeartbeatMsg>(topics::SAFETY_HEARTBEAT, RID_SAFETY_HB, serHeartbeat);
    util::Network::GetInstance().RegisterIncomingTopic<HeartbeatMsg>(topics::CONTROLLER_HEARTBEAT, RID_CONTROLLER_HB, serHeartbeat);

    // Outgoing Topics
    util::Network::GetInstance().AddRemoteNode("Controller", "127.0.0.1", 5011);
    util::Network::GetInstance().AddRemoteNode("Safety", "127.0.0.1", 5013);

    util::Network::GetInstance().RegisterOutgoingTopic<StartProcessMsg>(topics::CMD_RUN, RID_START_PROCESS, serStart, util::Network::Reliability::RELIABLE);
    util::Network::GetInstance().RegisterOutgoingTopic<StopProcessMsg>(topics::CMD_ABORT, RID_STOP_PROCESS, serStop, util::Network::Reliability::RELIABLE);
    util::Network::GetInstance().RegisterOutgoingTopic<HeartbeatMsg>(topics::GUI_HEARTBEAT, RID_GUI_HB, serHeartbeat);
    util::Network::GetInstance().RegisterOutgoingTopic<FaultMsg>(topics::FAULT, RID_FAULT_EVENT, serFault, util::Network::Reliability::RELIABLE);
}

void System::SetupWatchdog() {
    m_heartbeat.MonitorNode(topics::CONTROLLER_HEARTBEAT, FAULT_CONTROLLER_LOST, "Controller");
    m_heartbeat.MonitorNode(topics::SAFETY_HEARTBEAT, FAULT_SAFETY_LOST, "Safety");
}

void System::StartTimerThread() {
    m_timerRunning = true;
    m_backgroundTimer.CreateThread();
    (void)dmq::MakeDelegate(this, &System::TimerTick, m_backgroundTimer).AsyncInvoke();
}

void System::TimerTick() {
    Timer::ProcessTimers();
    Tick(100);
    if (m_timerRunning) {
        Thread::Sleep(std::chrono::milliseconds(100));
        (void)dmq::MakeDelegate(this, &System::TimerTick, m_backgroundTimer).AsyncInvoke();
    }
}

} // namespace cellutron
