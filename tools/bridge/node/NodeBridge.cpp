// NodeBridge.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Desktop front end of dmq::databus::NodeReporter.

#include "NodeBridge.h"
#include "extras/util/NetworkConnect.h"
#include <iostream>

dmq::databus::NodeReporter& NodeBridge::Reporter() {
    auto& instance = GetInstance();
    if (!instance.reporter)
        instance.reporter = std::make_unique<dmq::databus::NodeReporter>();
    return *instance.reporter;
}

void NodeBridge::Start(const std::string& nodeId, const std::string& address, uint16_t port) {
    Init(nodeId, address, port, false, "");
}

void NodeBridge::StartMulticast(const std::string& nodeId, const std::string& groupAddr, uint16_t port, const std::string& localInterface) {
    Init(nodeId, groupAddr, port, true, localInterface);
}

void NodeBridge::Init(const std::string& nodeId, const std::string& address, uint16_t port,
                      bool isMulticast, const std::string& localInterface) {
    auto& instance = GetInstance();
    if (instance.started) return;

    if (!instance.transport.Open(address, port, isMulticast, localInterface)) {
        std::cerr << "[NodeBridge] ERROR - could not open UDP to " << address << ":" << port << std::endl;
        return;
    }

    char hostname[256] = {};
    if (gethostname(hostname, sizeof(hostname)) != 0)
        hostname[0] = '\0';

    // Self-reported address: the multicast interface if one was given, else the
    // first non-loopback IPv4 address. dmq-monitor keys its rows on nodeId + this,
    // so same-named nodes on different machines get separate rows.
    const std::string ipAddress = (isMulticast && !localInterface.empty() && localInterface != "0.0.0.0")
        ? localInterface
        : std::string(dmq::util::NetworkContext::GetLocalAddress().c_str());

    if (!Reporter().Start(instance.transport, nodeId.c_str(), ipAddress.c_str(), hostname)) {
        std::cerr << "[NodeBridge] ERROR - failed to start the reporter thread" << std::endl;
        instance.transport.Close();
        return;
    }
    instance.started = true;
}

void NodeBridge::Stop() {
    auto& instance = GetInstance();
    if (instance.reporter) {
        instance.reporter->Stop();      // also drops all Watch() connections
        instance.reporter.reset();
    }
    if (instance.started) {
        instance.transport.Close();
        instance.started = false;
    }
}

uint32_t NodeBridge::GetDropped() {
    auto& instance = GetInstance();
    return instance.reporter ? instance.reporter->GetDropped() : 0;
}
