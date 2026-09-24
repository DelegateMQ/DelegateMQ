#ifndef PUMPTRON_LINK_ERROR_REPORTER_H
#define PUMPTRON_LINK_ERROR_REPORTER_H

#include "DelegateMQ.h"
#include "pump/PumpController.h"
#include "util/Constants.h"
#include <cstdio>

namespace pumptron {
namespace pump {

/// @brief Routes every DelegateMQ error/status channel on the controller to
///        PumpController.
///
/// Used by both platform mains, so the simulator (NetworkNode<UDP>) and the F4
/// (SerialLink<Stm32UartTransport>) report errors identically:
/// - DataBus::SubscribeError        -> LINK_DEGRADED
/// - DataBus::SubscribeUnhandled    -> log only (a wiring bug, not a link fault)
/// - link OnDeliveryFailed          -> LINK_DEGRADED, and a status/alarm resync
///                                     if the abandoned message was status or alarm
/// - link OnPeerCapExceeded /
///   OnPeerPendingExceeded          -> LINK_DEGRADED
/// - send-queue drops (SerialLink)  -> LINK_DEGRADED, via WatchSendDropped()
///
/// Owns the connections: keep it alive as long as the link.
class LinkErrorReporter {
public:
    template <typename Link>
    LinkErrorReporter(Link& link, PumpController& pump) : m_pump(pump)
    {
        m_busErrorConn = dmq::databus::DataBus::SubscribeError(
            dmq::MakeDelegate(this, &LinkErrorReporter::OnBusError));
        m_unhandledConn = dmq::databus::DataBus::SubscribeUnhandled(
            dmq::MakeDelegate(this, &LinkErrorReporter::OnUnhandled));
        m_deliveryConn = link.OnDeliveryFailed.Connect(
            dmq::MakeDelegate(this, &LinkErrorReporter::OnDeliveryFailed));
        m_capConn = link.OnPeerCapExceeded.Connect(
            dmq::MakeDelegate(this, &LinkErrorReporter::OnCapExceeded));
        m_pendingConn = link.OnPeerPendingExceeded.Connect(
            dmq::MakeDelegate(this, &LinkErrorReporter::OnPendingExceeded));
    }

    /// SerialLink only: frames dropped because its send queue was full.
    void WatchSendDropped(dmq::Signal<void(size_t)>& onSendDropped)
    {
        m_droppedConn = onSendDropped.Connect(
            dmq::MakeDelegate(this, &LinkErrorReporter::OnSendDropped));
    }

    LinkErrorReporter(const LinkErrorReporter&) = delete;
    LinkErrorReporter& operator=(const LinkErrorReporter&) = delete;

private:
    void OnBusError(const dmq::xstring& topic, dmq::DelegateError error)
    {
        printf("Controller: DataBus error topic=%s error=%d\n", topic.c_str(), static_cast<int>(error));
        m_pump.ReportLinkError("DataBus error");
    }

    void OnUnhandled(const dmq::xstring& topic)
    {
        printf("Controller: WARNING - published with no subscriber: %s\n", topic.c_str());
    }

    void OnDeliveryFailed(const dmq::xstring& peer, dmq::DelegateRemoteId id, uint16_t seq)
    {
        printf("Controller: delivery to %s failed (id=%u seq=%u)\n", peer.c_str(), id, seq);
        m_pump.ReportLinkError("delivery failed");
        if (id == RID_STATUS || id == RID_ALARM)
            m_pump.RequestResync();
    }

    void OnCapExceeded(const dmq::xstring&, size_t)       { m_pump.ReportLinkError("unacked-message cap exceeded"); }
    void OnPendingExceeded(const dmq::xstring&, size_t)   { m_pump.ReportLinkError("retry backlog not draining"); }
    void OnSendDropped(size_t)                             { m_pump.ReportLinkError("send queue full, frame dropped"); }

    PumpController& m_pump;
    dmq::ScopedConnection m_busErrorConn;
    dmq::ScopedConnection m_unhandledConn;
    dmq::ScopedConnection m_deliveryConn;
    dmq::ScopedConnection m_capConn;
    dmq::ScopedConnection m_pendingConn;
    dmq::ScopedConnection m_droppedConn;
};

} // namespace pump
} // namespace pumptron

#endif
