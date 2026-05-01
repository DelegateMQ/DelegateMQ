#include "Heartbeat.h"
#include "messages/FaultMsg.h"
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
    m_timerConn = m_timer.OnExpired.Connect(dmq::MakeDelegate(this, &Heartbeat::OnTimerExpired, m_thread));
    m_timer.Start(HEARTBEAT_PERIOD);
}

void Heartbeat::MonitorNode(const char* remoteTopic, FaultCode faultCode, const std::string& nodeName)
{
    auto sub = std::make_unique<dmq::databus::DeadlineSubscription<HeartbeatMsg>>(
        remoteTopic,
        HEARTBEAT_TIMEOUT,
        [](const HeartbeatMsg&) { /* No-op on success */ },
        [this, nodeName, faultCode]() {
            this->OnMonitorTimeout(nodeName, faultCode);
        },
        &m_thread
    );

    m_monitors.push_back({ nodeName, std::move(sub) });
}

void Heartbeat::Tick(uint32_t ms)
{
    m_msElapsed += ms;
}

void Heartbeat::OnTimerExpired()
{
    dmq::databus::DataBus::Publish<HeartbeatMsg>(m_localTopic, { ++m_counter });
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
