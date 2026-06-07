#include "System.h"
#include "ui/UI.h"
#include "logs/Logs.h"
#include "alarms/Alarms.h"
#include "RemoteConfig.h"
#include "Constants.h"
#include "extras/util/ThreadMonitor.h"
#include "extras/util/ThreadMonitorSer.h"
#include "SpyBridge.h"
#include "NodeBridge.h"
#include <cstdio>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {

System::System()
    : m_thread("GUI_SystemThread", dmq::DEFAULT_QUEUE_SIZE, FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "GUI")
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
        m_network.Stop();
    }
}

void System::Tick(uint32_t ms) {
    m_heartbeat.Tick(ms);
}

void System::SetupNetwork() {
    SpyBridge::Start("127.0.0.1", 9999, "GUI");
    NodeBridge::StartMulticast("GUI", "239.1.1.1", 9998);

    m_network.Start("GUI", /*listenPort=*/5010);

    // Incoming Topics
    m_network.Receive<RunStatusMsg>      (topics::STATUS_RUN,          RID_RUN_STATUS,       serRun);
    m_network.Receive<CentrifugeSpeedMsg>(topics::CMD_CENTRIFUGE_SPEED, RID_CENTRIFUGE_SPEED, serSpeed);
    m_network.Receive<CentrifugeSpeedMsg>(topics::RPM,                  RID_CENTRIFUGE_STATUS, serSpeed);
    m_network.Receive<FaultMsg>          (topics::FAULT,               RID_FAULT_EVENT,      serFault);
    m_network.Receive<ActuatorStatusMsg> (topics::STATUS_ACTUATOR,     RID_ACTUATOR_STATUS,  serActuator);
    m_network.Receive<SensorStatusMsg>   (topics::STATUS_SENSOR,       RID_SENSOR_STATUS,    serSensor);
    m_network.Receive<HeartbeatMsg>      (topics::SAFETY_HEARTBEAT,    RID_SAFETY_HB,        serHeartbeat);
    m_network.Receive<HeartbeatMsg>      (topics::CONTROLLER_HEARTBEAT, RID_CONTROLLER_HB,   serHeartbeat);

    // Remote peers
    m_network.AddPeer("Controller", "127.0.0.1", /*udpPort=*/5011);
    m_network.AddPeer("Safety",     "127.0.0.1", /*udpPort=*/5013);

    // Outgoing Topics — commands use RELIABLE (ACK + retry); heartbeat uses UNRELIABLE
    using Rel = dmq::databus::Reliability;
    m_network.Send<StartProcessMsg>(topics::CMD_RUN,      RID_START_PROCESS, serStart,    Rel::RELIABLE);
    m_network.Send<StopProcessMsg> (topics::CMD_ABORT,    RID_STOP_PROCESS,  serStop,     Rel::RELIABLE);
    m_network.Send<FaultMsg>       (topics::FAULT,        RID_FAULT_EVENT,   serFault,    Rel::RELIABLE);
    m_network.Send<HeartbeatMsg>   (topics::GUI_HEARTBEAT, RID_GUI_HB,       serHeartbeat);
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
    Tick(static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(TIMER_TICK_PERIOD).count()));
    if (m_timerRunning) {
        Thread::Sleep(TIMER_TICK_PERIOD);
        (void)dmq::MakeDelegate(this, &System::TimerTick, m_backgroundTimer).AsyncInvoke();
    }
}

} // namespace cellutron
