#ifndef _HEARTBEAT_H
#define _HEARTBEAT_H

#include "DelegateMQ.h"
#include "messages/HeartbeatMsg.h"
#include "util/Constants.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>

namespace cellutron {
namespace util {

/// @brief Generalized heartbeat emitter and monitor component.
/// 
/// This class handles periodic publication of a local heartbeat and monitors 
/// multiple remote heartbeat topics for "Loss of Signal" (LOS) conditions.
class Heartbeat {
public:
    /// @param name The human-readable name of this node (for logging).
    /// @param localTopic The topic this node publishes its heartbeat on.
    /// @param thread The thread to run emitter and monitor callbacks on.
    Heartbeat(const std::string& name, const char* localTopic, dmq::os::Thread& thread);
    ~Heartbeat();

    /// Start the heartbeat emitter.
    void Start();

    /// Add a remote node to monitor.
    /// @param remoteTopic The topic to monitor.
    /// @param faultCode The fault code to publish if this node is lost.
    /// @param nodeName The human-readable name of the monitored node.
    void MonitorNode(const char* remoteTopic, FaultCode faultCode, const std::string& nodeName);

    /// Update the internal tick counter.
    /// @param ms Milliseconds elapsed since last tick.
    void Tick(uint32_t ms);

private:
    void OnTimerExpired();
    void OnMonitorTimeout(const std::string& nodeName, FaultCode faultCode);
    void TriggerFault(const std::string& nodeName, FaultCode faultCode);

    std::string m_name;
    const char* m_localTopic;
    dmq::os::Thread&     m_thread;

    dmq::util::Timer       m_timer;
    dmq::ScopedConnection m_timerConn;
    uint32_t    m_counter = 0;

    struct Monitor {
        std::string name;
        std::unique_ptr<dmq::databus::DeadlineSubscription<HeartbeatMsg>> subscription;
    };
    std::vector<Monitor> m_monitors;

    std::atomic<uint32_t> m_msElapsed{0};
    bool     m_faultTriggered = false;
};

} // namespace util
} // namespace cellutron

#endif // _HEARTBEAT_H
