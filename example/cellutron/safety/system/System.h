#ifndef _SAFETY_SYSTEM_H
#define _SAFETY_SYSTEM_H

#include "DelegateMQ.h"
#include "util/Heartbeat.h"
#include "util/MessageGuard.h"
#include "util/NetworkTypes.h"

namespace cellutron {

struct CentrifugeSpeedMsg;
struct FaultMsg;

/// @brief Top-level system coordinator for the Safety node.
class System {
public:
    static System& GetInstance() {
        static System instance;
        return instance;
    }

    void Initialize();
    void Shutdown();
    void Tick(uint32_t ms);

    dmq::os::Thread& GetThread() { return m_thread; }

private:
    System();
    ~System() = default;

    System(const System&) = delete;
    System& operator=(const System&) = delete;

    void SetupLocalSubscriptions();
    void SetupNetwork();
    void SetupWatchdog();

    void OnSpeed(CentrifugeSpeedMsg msg);
    void OnFault(FaultMsg msg);

    void OnDataBusError(const dmq::xstring& topic, dmq::DelegateError error);
    void OnDeliveryFailed(const dmq::xstring& peerName, dmq::DelegateRemoteId id, uint16_t seqNum);
    void OnPeerCapExceeded(const dmq::xstring& peerName, size_t count);
    void OnPeerPendingExceeded(const dmq::xstring& peerName, size_t remaining);
    void OnPeerSendStatus(const dmq::xstring& peerName, dmq::DelegateRemoteId id, uint16_t seqNum,
                           dmq::util::TransportMonitor::Status status);

    dmq::os::Thread m_thread;

    Network m_network;

    dmq::ScopedConnection m_speedConn;
    dmq::ScopedConnection m_faultConn;

    // Error/status reporting connections
    dmq::ScopedConnection m_dataBusErrorConn;
    dmq::ScopedConnection m_deliveryFailedConn;
    dmq::ScopedConnection m_capExceededConn;
    dmq::ScopedConnection m_pendingExceededConn;
    dmq::ScopedConnection m_sendStatusConn;

    MessageGuard m_speedGuard;

    util::Heartbeat m_heartbeat;
    bool      m_faulted = false;
};

} // namespace cellutron

#endif // _SAFETY_SYSTEM_H
