#include "System.h"
#include "Network.h"
#include "RemoteConfig.h"
#include "Constants.h"
#include <cstdio>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {

void System::Initialize() {
    printf("Safety: System initializing...\n");

    cellutron::RegisterSerializers();
    cellutron::RegisterStringifiers();
    
    m_thread.SetThreadPriority(PRIORITY_PROCESS);
    m_thread.CreateThread(WATCHDOG_TIMEOUT);

    SetupLocalSubscriptions();
    SetupNetwork();
    SetupWatchdog();

    m_heartbeat.Start();

    printf("Safety: System ready.\n");
}

void System::Tick(uint32_t ms) {
    m_heartbeat.Tick(ms);
}

void System::SetupLocalSubscriptions() {
    // Intentional safety latch: once faulted, the fault is NOT cleared automatically.
    // Recovery requires a deliberate operator action (system restart). This prevents
    // the safety node from silently resuming enforcement after a fault event.
    m_speedConn = dmq::databus::DataBus::Subscribe<CentrifugeSpeedMsg>(topics::CMD_CENTRIFUGE_SPEED, [this](CentrifugeSpeedMsg msg) {
        if (!m_speedGuard.IsNewer(msg.seq)) {
            return;
        }

        if (msg.rpm > MAX_CENTRIFUGE_RPM) {
            if (!m_faulted) {
                m_faulted = true;
                printf("Safety: CRITICAL - Centrifuge speed exceeded limit! (%u RPM). TRIGGERING FAULT.\n", msg.rpm);
                dmq::databus::DataBus::Publish<FaultMsg>(topics::FAULT, { FAULT_OVERSPEED });
            } else {
                printf("Safety: Overspeed still active (%u RPM). System already faulted.\n", msg.rpm);
            }
        }
    }, &m_thread);

    m_faultConn = dmq::databus::DataBus::Subscribe<FaultMsg>(topics::FAULT, [this](FaultMsg) {
        // NOTE: IsNewer() guard intentionally omitted for faults. Prioritize safety
        // over ordering; a "nuisance trip" from an old fault is safer than missing 
        // a trip due to a sequencing race.
        m_faulted = true;
    }, &m_thread);
}

void System::SetupNetwork() {
    util::Network::GetInstance().Initialize(5013, "Safety"); 
    util::Network::GetInstance().RegisterIncomingTopic<CentrifugeSpeedMsg>(topics::CMD_CENTRIFUGE_SPEED, RID_CENTRIFUGE_SPEED, serSpeed);
    util::Network::GetInstance().RegisterIncomingTopic<HeartbeatMsg>(topics::CONTROLLER_HEARTBEAT, RID_CONTROLLER_HB, serHeartbeat);
    util::Network::GetInstance().RegisterIncomingTopic<HeartbeatMsg>(topics::GUI_HEARTBEAT, RID_GUI_HB, serHeartbeat);
    util::Network::GetInstance().RegisterIncomingTopic<FaultMsg>(topics::FAULT, RID_FAULT_EVENT, serFault);

    util::Network::GetInstance().AddRemoteNode("Controller", "127.0.0.1", 5011);
    util::Network::GetInstance().AddRemoteNode("GUI", "127.0.0.1", 5010);

    util::Network::GetInstance().RegisterOutgoingTopic<FaultMsg>(topics::FAULT, RID_FAULT_EVENT, serFault, util::Network::Reliability::RELIABLE);
    util::Network::GetInstance().RegisterOutgoingTopic<HeartbeatMsg>(topics::SAFETY_HEARTBEAT, RID_SAFETY_HB, serHeartbeat);
}

void System::SetupWatchdog() {
    m_heartbeat.MonitorNode(topics::CONTROLLER_HEARTBEAT, FAULT_CONTROLLER_LOST, "Controller");
    m_heartbeat.MonitorNode(topics::GUI_HEARTBEAT, FAULT_GUI_LOST, "GUI");
}

} // namespace cellutron
