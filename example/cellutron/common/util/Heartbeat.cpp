#include "Heartbeat.h"
#include "Logger.h"
#include "messages/FaultMsg.h"
#include "extras/util/Fault.h"
#include <cstdio>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace cellutron {
namespace util {

Heartbeat::Heartbeat(const std::string& name, const char* localTopic, dmq::os::Thread& thread) :
    m_name(name),
    m_localTopic(localTopic),
    m_thread(thread)
{
}

Heartbeat::~Heartbeat()
{
}

void Heartbeat::Start()
{
    m_timerConn = m_timer.OnExpired.Connect(dmq::util::MakeTimerDelegate(this, &Heartbeat::OnTimerExpired, m_thread));
    m_timer.Start(HEARTBEAT_PERIOD);
}

void Heartbeat::MonitorNode(const char* remoteTopic, FaultCode faultCode, const std::string& nodeName)
{
    ASSERT_TRUE(m_monitorCount < m_monitors.size());

    Monitor& monitor = m_monitors[m_monitorCount++];
    monitor.name = nodeName;
    monitor.faultCode = faultCode;
    monitor.parent = this;

    // Use explicit 'new' to ensure the XALLOCATOR overloaded operator new is called.
    // std::make_unique and dmq::xmake_shared bypass class-specific operator new.
    // Use delegates instead of lambdas per project standards.
    monitor.subscription.reset(new dmq::databus::DeadlineSubscription<HeartbeatMsg>(
        remoteTopic,
        HEARTBEAT_TIMEOUT,
        dmq::MakeDelegate(this, &Heartbeat::OnHeartbeatReceived),
        dmq::MakeDelegate(&monitor, &Monitor::OnTimeout),
        &m_thread
    ));
}

void Heartbeat::Tick(uint32_t ms)
{
    m_msElapsed += ms;
}

void Heartbeat::OnTimerExpired()
{
    dmq::databus::DataBus::Publish<HeartbeatMsg>(m_localTopic, { ++m_counter });
}

void Heartbeat::OnHeartbeatReceived(const HeartbeatMsg&)
{
    // No-op
}

void Heartbeat::OnMonitorTimeout(const std::string& nodeName, FaultCode faultCode)
{
    // Ignore timeouts during the warmup period to allow all nodes to boot
    if (m_msElapsed.load() < HEARTBEAT_WARMUP.count()) {
        return;
    }

    TriggerFault(nodeName, faultCode);
}

void Heartbeat::TriggerFault(const std::string& nodeName, FaultCode faultCode)
{
    // Protect against "fault storm" - only trigger once locally per instance
    if (m_faultTriggered) {
        return;
    }

    m_faultTriggered = true;

    printf("[%s] CRITICAL - %s heartbeat lost! TRIGGERING FAULT.\n", m_name.c_str(), nodeName.c_str());
    
    // Publish fault to network
    dmq::databus::DataBus::Publish<FaultMsg>(topics::FAULT, { static_cast<uint8_t>(faultCode) });
}

} // namespace util
} // namespace cellutron
