#ifndef SENSOR_NODE_MAIN_H
#define SENSOR_NODE_MAIN_H

/// @file NodeMain.h
/// @brief The sensor node's program, the same on every platform. A platform's
/// main() only creates its hardware (Hal.h), brings up its network and calls
/// RunNode().

#include "Hal.h"
#include "SensorNode.h"
#include "DelegateMQ.h"

#include <atomic>

namespace sensornode {

/// Start the node, then run the platform loop on the calling thread:
/// dmq::util::Timer::ProcessTimers() and IsrDelegates::ProcessAll() every
/// millisecond, Thread::WatchdogCheckAll() every 100 ms, a summary every 10 s.
/// Stop after runFor (zero: no limit) or when *stop becomes true, then check
/// the counters and print NODE CHECKS PASSED or FAILED.
/// @return true if the checks passed. With no limit and no stop flag, never returns.
bool RunNode(Platform& platform, const NodeConfig& config,
             dmq::Duration runFor = dmq::Duration::zero(),
             const std::atomic<bool>* stop = nullptr);

} // namespace sensornode

#endif // SENSOR_NODE_MAIN_H
