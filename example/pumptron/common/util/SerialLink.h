#ifndef PUMPTRON_SERIAL_LINK_H
#define PUMPTRON_SERIAL_LINK_H

/// @file SerialLink.h
/// @brief Point-to-point DataBus link over one full-duplex byte-stream transport.
///
/// @details
/// `dmq::databus::NetworkNode` is built around address/port transports (UDP):
/// one listen socket plus one socket per peer. A serial link is different --
/// a single port carries both directions to exactly one peer. SerialLink
/// provides the same `Send<T>()` / `Receive<T>()` wiring API and the same
/// status signals as NetworkNode, so topology code (see Topology.h) is written
/// once and applied to either link type.
///
/// Reliability tiers, same as NetworkNode:
/// - `Reliability::RELIABLE`   -- ACK + retry via RetryMonitor/ReliableTransport.
/// - `Reliability::UNRELIABLE` -- fire-and-forget.
///
/// Threads:
/// - Send thread (`GetSendThread()`): serializes and writes outgoing frames and
///   runs TransportMonitor::Process() for retry timeouts. FullPolicy::DROP --
///   a stalled link drops outgoing frames rather than faulting the node
///   (reported via OnSendDropped); RELIABLE frames that were already handed to
///   the RetryMonitor keep being retried.
/// - Receive: either an internal timer-driven thread (`RecvMode::INTERNAL_THREAD`,
///   for transports whose Receive() returns on a short timeout, e.g. the
///   libserialport SerialTransport), or the caller drives `Poll()` from its own
///   task (`RecvMode::EXTERNAL_POLL`, for transports whose Receive() blocks
///   until data arrives, e.g. Stm32UartTransport).
///
/// @tparam Transport  ITransport with SetTransportMonitor() and Close(). Its
///                    platform-specific Create() is called by the owner via
///                    GetTransport() before Start().

#include "DelegateMQ.h"
#include "extras/databus/NetworkNode.h"
#include "extras/util/ReliableTransport.h"
#include "extras/util/RetryMonitor.h"
#include "extras/util/TransportMonitor.h"
#include <array>
#include <cstring>
#include <memory>
#include <optional>

namespace pumptron {

template <typename Transport, size_t MaxTopics = dmq::NETWORK_NODE_MAX_TOPICS>
class SerialLink
{
    using DataBus          = dmq::databus::DataBus;
    using Participant      = dmq::databus::Participant;
    using Reliability      = dmq::databus::Reliability;
    using TransportMonitor = dmq::util::TransportMonitor;

public:
    /// Same signatures as NetworkNode so status handlers are link-agnostic.
    dmq::Signal<void(const dmq::xstring&, size_t)> OnPeerCapExceeded;
    dmq::Signal<void(const dmq::xstring&, size_t)> OnPeerPendingExceeded;
    dmq::Signal<void(const dmq::xstring&, dmq::DelegateRemoteId, uint16_t)> OnDeliveryFailed;
    dmq::Signal<void(const dmq::xstring&, dmq::DelegateRemoteId, uint16_t, TransportMonitor::Status)> OnPeerSendStatus;

    /// Fired (on the publishing thread) when an outgoing frame is dropped
    /// because the send queue is full.
    dmq::Signal<void(size_t)> OnSendDropped;

    enum class RecvMode { INTERNAL_THREAD, EXTERNAL_POLL };

    SerialLink()
        : m_sendThread("LinkTx", dmq::DEFAULT_QUEUE_SIZE, dmq::os::FullPolicy::DROP)
        , m_recvThread("LinkRx", dmq::DEFAULT_QUEUE_SIZE, dmq::os::FullPolicy::DROP)
    {
    }

    ~SerialLink() { Stop(); }

    SerialLink(const SerialLink&) = delete;
    SerialLink& operator=(const SerialLink&) = delete;

    /// Underlying transport; call its platform-specific Create() before Start().
    Transport& GetTransport() { return m_transport; }

    /// Exposed so an RTOS target can set priority/stack before Start().
    dmq::os::Thread& GetSendThread() { return m_sendThread; }
    dmq::os::Thread& GetRecvThread() { return m_recvThread; }

    /// @brief Wire the reliability stack, register with DataBus and start threads.
    /// @param peerName  Name of the remote end, used in status signals.
    /// @param mode      Who drives the receive side (see class notes).
    /// @param watchdog  Optional watchdog timeout for the link threads.
    /// @return true if the link is running.
    /// @note One-shot: a SerialLink starts at most once. Calling Start() again
    ///       while running returns true; calling it during or after Stop()
    ///       returns false instead of re-initializing a link that is being (or
    ///       has been) torn down. Create a new SerialLink to reconnect.
    bool Start(const char* peerName, RecvMode mode,
               std::optional<dmq::Duration> watchdog = std::nullopt)
    {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_started) return m_running;
        m_started = true;

        m_peerName = peerName;

        // Reliability stack: raw transport <- TransportMonitor <- RetryMonitor <- ReliableTransport.
        // The raw transport's Receive() removes ACKed sequence numbers from the
        // monitor and auto-ACKs every incoming data frame.
        m_transport.SetTransportMonitor(&m_monitor);
        m_retry.Init(m_transport, m_monitor);
        m_reliable.Init(m_transport, m_retry);

        m_capConn = m_monitor.OnCapExceeded.Connect(
            dmq::MakeDelegate(this, &SerialLink::HandleCapExceeded));
        m_pendingConn = m_monitor.OnPendingExceeded.Connect(
            dmq::MakeDelegate(this, &SerialLink::HandlePendingExceeded));
        m_sendStatusConn = m_monitor.OnSendStatus.Connect(
            dmq::MakeDelegate(this, &SerialLink::HandleSendStatus));
        m_deliveryFailedConn = m_retry.OnDeliveryFailed.Connect(
            dmq::MakeDelegate(this, &SerialLink::HandleDeliveryFailed));

        // One inbound participant (all incoming topics) and one outbound
        // participant per reliability tier -- all sharing the one port.
        m_rxParticipant         = dmq::xmake_shared<Participant>(m_transport);
        m_reliableParticipant   = dmq::xmake_shared<Participant>(m_reliable);
        m_unreliableParticipant = dmq::xmake_shared<Participant>(m_transport);

        for (size_t i = 0; i < m_inCount; ++i)
            m_inTopics[i].adder(m_inTopics[i].serializer, m_inTopics[i].topic,
                                m_inTopics[i].remoteId, *m_rxParticipant);
        for (size_t i = 0; i < m_outCount; ++i)
            ApplyOutgoing(m_outTopics[i]);

        m_sendThread.SetDroppedHandler(dmq::MakeDelegate(this, &SerialLink::HandleSendDropped));
        if (!m_sendThread.CreateThread(watchdog))
            return false;

        m_reliableParticipant->SetSendThread(&m_sendThread);
        m_unreliableParticipant->SetSendThread(&m_sendThread);
        DataBus::AddParticipant(m_reliableParticipant);
        DataBus::AddParticipant(m_unreliableParticipant);

        // Retry/timeout bookkeeping runs on the send thread.
        m_monitorConn = m_monitorTimer.OnExpired.Connect(
            dmq::util::MakeTimerDelegate(this, &SerialLink::ProcessMonitor, m_sendThread));
        m_monitorTimer.Start(std::chrono::milliseconds(100));

        if (mode == RecvMode::INTERNAL_THREAD) {
            if (!m_recvThread.CreateThread(watchdog))
                return false;
            m_recvConn = m_recvTimer.OnExpired.Connect(
                dmq::util::MakeTimerDelegate(this, &SerialLink::PollTick, m_recvThread));
            m_recvTimer.Start(std::chrono::milliseconds(5));
            m_ownRecvThread = true;
        }

        m_running = true;
        return true;
    }

    /// @brief Stop threads, unregister from DataBus and close the transport.
    void Stop()
    {
        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
            if (!m_running) return;
            m_running = false;
            m_recvTimer.Stop();
            m_monitorTimer.Stop();
        }

        // Exit threads with no lock held: an already-queued PollTick/ProcessMonitor
        // may still run to completion inside ExitThread() (see NetworkNode::Stop()).
        if (m_ownRecvThread)
            m_recvThread.ExitThread();
        m_sendThread.ExitThread();
        m_recvConn.Disconnect();
        m_monitorConn.Disconnect();

        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        DataBus::RemoveParticipant(m_reliableParticipant);
        DataBus::RemoveParticipant(m_unreliableParticipant);
        m_reliableParticipant->SetSendThread(nullptr);
        m_unreliableParticipant->SetSendThread(nullptr);
        m_capConn.Disconnect();
        m_pendingConn.Disconnect();
        m_sendStatusConn.Disconnect();
        m_deliveryFailedConn.Disconnect();
        m_transport.Close();
    }

    /// @brief Process up to `maxFrames` incoming frames.
    /// @details Call repeatedly from a dedicated task when started with
    ///          RecvMode::EXTERNAL_POLL. Blocks inside the transport's Receive()
    ///          exactly as long as that transport blocks.
    /// @return Number of frames successfully processed.
    int Poll(int maxFrames = dmq::NETWORK_NODE_MAX_WORK)
    {
        if (!m_rxParticipant) return 0;
        int n = 0;
        for (; n < maxFrames; ++n)
            if (m_rxParticipant->ProcessIncoming() != 0) break;
        return n;
    }

    /// @brief Register a topic this node publishes to the peer.
    template <typename T>
    void Send(const dmq::xstring& topic, dmq::DelegateRemoteId remoteId,
              dmq::ISerializer<void(T)>& serializer,
              Reliability rel = Reliability::UNRELIABLE)
    {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        DMQ_ASSERT_TRUE(m_outCount < MaxTopics);
        DataBus::RegisterSerializer<T>(topic, serializer);
        m_outTopics[m_outCount] = { topic, remoteId, rel };
        if (m_reliableParticipant)
            ApplyOutgoing(m_outTopics[m_outCount]);
        m_outCount++;
    }

    /// @brief Register a topic this node receives from the peer.
    template <typename T>
    void Receive(const dmq::xstring& topic, dmq::DelegateRemoteId remoteId,
                 dmq::ISerializer<void(T)>& serializer)
    {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        DMQ_ASSERT_TRUE(m_inCount < MaxTopics);
        m_inTopics[m_inCount++] = {
            topic, remoteId, static_cast<void*>(&serializer),
            [](void* ser, const dmq::xstring& t, dmq::DelegateRemoteId rid, Participant& p) {
                DataBus::AddIncomingTopic<T>(t, rid, p, *static_cast<dmq::ISerializer<void(T)>*>(ser));
            }
        };
        if (m_rxParticipant)
            DataBus::AddIncomingTopic<T>(topic, remoteId, *m_rxParticipant, serializer);
    }

private:
    struct OutgoingTopic {
        dmq::xstring          topic;
        dmq::DelegateRemoteId remoteId    = 0;
        Reliability           reliability = Reliability::UNRELIABLE;
    };

    using TopicAdder = void(*)(void*, const dmq::xstring&, dmq::DelegateRemoteId, Participant&);

    struct IncomingTopic {
        dmq::xstring          topic;
        dmq::DelegateRemoteId remoteId   = 0;
        void*                 serializer = nullptr;
        TopicAdder            adder      = nullptr;
    };

    void ApplyOutgoing(const OutgoingTopic& out)
    {
        if (out.reliability == Reliability::RELIABLE)
            m_reliableParticipant->AddRemoteTopic(out.topic, out.remoteId);
        else
            m_unreliableParticipant->AddRemoteTopic(out.topic, out.remoteId);
    }

    void PollTick() { Poll(); }
    void ProcessMonitor() { m_monitor.Process(); }

    void HandleCapExceeded(size_t n) { OnPeerCapExceeded(m_peerName, n); }
    void HandlePendingExceeded(size_t n) { OnPeerPendingExceeded(m_peerName, n); }
    void HandleSendStatus(dmq::DelegateRemoteId id, uint16_t seq, TransportMonitor::Status status) {
        OnPeerSendStatus(m_peerName, id, seq, status);
    }
    void HandleDeliveryFailed(dmq::DelegateRemoteId id, uint16_t seq) {
        OnDeliveryFailed(m_peerName, id, seq);
    }
    void HandleSendDropped(size_t depth) { OnSendDropped(depth); }

    Transport                       m_transport;
    TransportMonitor                m_monitor;
    dmq::util::RetryMonitor         m_retry;       // Init(m_transport, m_monitor)
    dmq::util::ReliableTransport    m_reliable;    // Init(m_transport, m_retry)

    std::shared_ptr<Participant>    m_rxParticipant;
    std::shared_ptr<Participant>    m_reliableParticipant;
    std::shared_ptr<Participant>    m_unreliableParticipant;

    std::array<IncomingTopic, MaxTopics> m_inTopics{};
    size_t m_inCount = 0;
    std::array<OutgoingTopic, MaxTopics> m_outTopics{};
    size_t m_outCount = 0;

    dmq::os::Thread         m_sendThread;
    dmq::os::Thread         m_recvThread;
    bool                    m_ownRecvThread = false;
    dmq::util::Timer        m_recvTimer;
    dmq::util::Timer        m_monitorTimer;
    dmq::ScopedConnection   m_recvConn;
    dmq::ScopedConnection   m_monitorConn;
    dmq::ScopedConnection   m_capConn;
    dmq::ScopedConnection   m_pendingConn;
    dmq::ScopedConnection   m_sendStatusConn;
    dmq::ScopedConnection   m_deliveryFailedConn;

    dmq::xstring            m_peerName;
    bool                    m_started = false;  ///< Set once by Start(); never cleared
    bool                    m_running = false;
    dmq::RecursiveMutex     m_mutex;
};

} // namespace pumptron

#endif // PUMPTRON_SERIAL_LINK_H
