// SpyBridge.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Desktop front end of dmq::databus::SpyReporter.

#include "SpyBridge.h"
#include "extras/util/ThreadMonitor.h"
#include <iostream>

namespace {
    /// Desktop bursts (a startup replay, a busy GUI) need more room than the
    /// library default, which is sized for a board.
    constexpr size_t DESKTOP_QUEUE_SIZE = 200;
}

void SpyBridge::Start(const std::string& address, uint16_t port, const std::string& nodeId) {
    Init(address, port, TransportType::UNICAST, nodeId);
    std::cout << "[SpyBridge] Starting Unicast bridge to " << address << ":" << port << " (Node: " << nodeId << ")" << std::endl;
}

void SpyBridge::StartMulticast(const std::string& groupAddr, uint16_t port, const std::string& nodeId, const std::string& localInterface) {
    Init(groupAddr, port, TransportType::MULTICAST, nodeId, localInterface);
    std::cout << "[SpyBridge] Starting Multicast bridge to " << groupAddr << ":" << port << " using interface " << localInterface << " (Node: " << nodeId << ")" << std::endl;
}

void SpyBridge::Init(const std::string& address, uint16_t port, TransportType type, const std::string& nodeId, const std::string& localInterface) {
    auto& instance = GetInstance();
    if (instance.bridge) return;

    if (!instance.transport.Open(address, port, type == TransportType::MULTICAST, localInterface))
        std::cerr << "[SpyBridge] ERROR - could not open UDP to " << address << ":" << port << std::endl;

    // The library bridge: DataBus::Monitor() on its own DROP-policy thread,
    // one framed SpyPacket per message through the socket above
    instance.bridge = std::make_unique<dmq::databus::SpyReporter>(DESKTOP_QUEUE_SIZE);
    dmq::util::ThreadMonitor::Register(&instance.bridge->GetThread());
    if (!instance.bridge->Start(instance.transport, nodeId.c_str()))
        std::cerr << "[SpyBridge] ERROR - failed to start the bridge thread" << std::endl;
}

void SpyBridge::Stop() {
    auto& instance = GetInstance();
    if (!instance.bridge) return;

    instance.bridge->Stop();
    // Init() registered the thread with ThreadMonitor: remove it before it's
    // destroyed, or the monitor's next poll reads a freed thread.
    dmq::util::ThreadMonitor::Deregister(&instance.bridge->GetThread());
    instance.bridge.reset();
    instance.transport.Close();
}

dmq::databus::SpyReporter* SpyBridge::GetReporter() {
    return GetInstance().bridge.get();
}

uint32_t SpyBridge::GetDropped() {
    auto& instance = GetInstance();
    return instance.bridge ? instance.bridge->GetDropped() : 0;
}
