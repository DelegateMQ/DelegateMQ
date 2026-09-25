#ifndef PUMPTRON_F4_CORE_DUMP_REPORTER_H
#define PUMPTRON_F4_CORE_DUMP_REPORTER_H

#include "DelegateMQ.h"
#include "CoreDump.h"
#include "util/Constants.h"
#include "messages/CoreDumpMsg.h"
#include "messages/HeartbeatMsg.h"
#include <atomic>
#include <cstdio>

namespace pumptron {

/// @brief Sends the core dump stored by the previous run to the GUI.
///
/// Waits for the GUI heartbeat (the GUI may not be connected at boot), then
/// publishes one CoreDumpMsg RELIABLE. The stored record is cleared only once
/// the GUI has ACKed it; if delivery fails after all retries, it is sent again
/// on the next GUI heartbeat. Does nothing if no dump is stored.
///
/// Callbacks: the GUI heartbeat arrives on the LinkRx task, send status and
/// delivery failure on the link's send thread; the atomic state covers both.
/// Owns its connections: keep it alive as long as the link.
class CoreDumpReporter {
public:
    template <typename Link>
    explicit CoreDumpReporter(Link& link)
    {
        if (!CoreDump_IsStored())
            return;
        m_state = State::PENDING;
        m_heartbeatConn = dmq::databus::DataBus::Subscribe<HeartbeatMsg>(topics::HB_GUI,
            dmq::MakeDelegate(this, &CoreDumpReporter::OnGuiHeartbeat));
        m_sendStatusConn = link.OnPeerSendStatus.Connect(
            dmq::MakeDelegate(this, &CoreDumpReporter::OnSendStatus));
        m_deliveryFailedConn = link.OnDeliveryFailed.Connect(
            dmq::MakeDelegate(this, &CoreDumpReporter::OnDeliveryFailed));
    }

    CoreDumpReporter(const CoreDumpReporter&) = delete;
    CoreDumpReporter& operator=(const CoreDumpReporter&) = delete;

private:
    enum class State : uint8_t { NONE, PENDING, IN_FLIGHT, DELIVERED };

    void OnGuiHeartbeat(const HeartbeatMsg&)
    {
        State expected = State::PENDING;
        if (!m_state.compare_exchange_strong(expected, State::IN_FLIGHT))
            return;

        CoreDumpMsg msg;
        if (!CoreDump_ToMsg(msg)) {
            m_state = State::NONE;
            return;
        }
        printf("Controller: GUI connected, sending core dump\n");
        dmq::databus::DataBus::Publish<CoreDumpMsg>(topics::CORE_DUMP, msg);
    }

    void OnSendStatus(const dmq::xstring&, dmq::DelegateRemoteId id, uint16_t,
                      dmq::util::TransportMonitor::Status status)
    {
        if (id != RID_CORE_DUMP || status != dmq::util::TransportMonitor::Status::SUCCESS)
            return;
        State expected = State::IN_FLIGHT;
        if (m_state.compare_exchange_strong(expected, State::DELIVERED)) {
            CoreDump_Clear();
            printf("Controller: core dump delivered to GUI\n");
        }
    }

    void OnDeliveryFailed(const dmq::xstring&, dmq::DelegateRemoteId id, uint16_t)
    {
        if (id != RID_CORE_DUMP)
            return;
        State expected = State::IN_FLIGHT;
        m_state.compare_exchange_strong(expected, State::PENDING);  // resend on next heartbeat
    }

    std::atomic<State> m_state{State::NONE};
    dmq::ScopedConnection m_heartbeatConn;
    dmq::ScopedConnection m_sendStatusConn;
    dmq::ScopedConnection m_deliveryFailedConn;
};

} // namespace pumptron

#endif
