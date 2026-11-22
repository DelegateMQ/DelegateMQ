#ifndef SENSOR_NODE_DESKTOP_TOOLS_H
#define SENSOR_NODE_DESKTOP_TOOLS_H

/// @file DesktopTools.h
/// @brief DelegateMQ's diagnostic tools for the desktop node (sensor_node --tools):
/// - dmq-spy (UDP 9999): every DataBus message the node publishes or receives
/// - dmq-monitor (multicast 239.1.1.1:9998): the node, its topics, link health
/// - dmq-thread (same multicast): the node's thread statistics
/// The bridges exist only in desktop builds (DMQ_DATABUS_TOOLS); elsewhere these
/// functions do nothing.

#include "SensorNode.h"
#include "SensorStringifiers.h"

#if defined(DMQ_DATABUS_TOOLS)
#include "SpyBridge.h"
#include "NodeBridge.h"
#include "extras/util/ThreadMonitor.h"
#endif

namespace sensornode::desktop {

/// Start the bridges for a running node (NodeConfig::onStarted)
inline void StartTools(SensorNode& node, const char* nodeName)
{
#if defined(DMQ_DATABUS_TOOLS)
    RegisterSensorStringifiers();       // values as text in dmq-spy
    SpyBridge::Start("127.0.0.1", 9999, nodeName);
    NodeBridge::StartMulticast(nodeName, "239.1.1.1", 9998);
    NodeBridge::Watch(node.GetTransportMonitor());
    if (dmq::util::RetryMonitor* retry = node.GetRetryMonitor())
        NodeBridge::Watch(*retry);
    for (dmq::os::Thread* thread : node.GetThreads())
        dmq::util::ThreadMonitor::Register(thread);
    dmq::util::ThreadMonitor::Enable();
    printf("SensorNode: diagnostics to dmq-spy (127.0.0.1:9999), dmq-monitor and dmq-thread (239.1.1.1:9998)\n");
#else
    (void)node; (void)nodeName;
#endif
}

/// Stop them before the node stops (NodeConfig::onStopping)
inline void StopTools(SensorNode& node)
{
#if defined(DMQ_DATABUS_TOOLS)
    dmq::util::ThreadMonitor::Disable();
    for (dmq::os::Thread* thread : node.GetThreads())
        dmq::util::ThreadMonitor::Deregister(thread);
    NodeBridge::Stop();
    SpyBridge::Stop();
#else
    (void)node;
#endif
}

} // namespace sensornode::desktop

#endif // SENSOR_NODE_DESKTOP_TOOLS_H
