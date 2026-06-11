#ifndef DMQ_NETWORK_NODE_H
#define DMQ_NETWORK_NODE_H

/// @file NetworkNode.h
/// @brief Generic multi-peer DataBus network node for any ITransport type.
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @details
/// NetworkNode wraps the full DataBus networking plumbing — transport setup,
/// Participant creation, reliability stacking, receive-loop thread — behind
/// a five-method API. A new user brings up a multi-process distributed DataBus
/// system by writing only a SetupNetwork() function:
///
/// @code
/// // In your node's System.cpp:
/// void System::SetupNetwork() {
///     m_network.Start("GUI", 5010);
///
///     m_network.Receive<HeartbeatMsg>(topics::SAFETY_HB, RID_SAFETY_HB, serHB);
///     m_network.Receive<FaultMsg>    (topics::FAULT,     RID_FAULT,      serFault);
///
///     m_network.AddPeer("Safety", "127.0.0.1", 5013);
///
///     m_network.Send<HeartbeatMsg>(topics::GUI_HB, RID_GUI_HB, serHB);
///     m_network.Send<FaultMsg>    (topics::FAULT,  RID_FAULT,  serFault,
///                                  dmq::databus::Reliability::RELIABLE);
/// }
/// // From here, use DataBus::Subscribe / DataBus::Publish normally.
/// @endcode
///
/// Topic ordering: Receive() and Send() may be called before or after Start() /
/// AddPeer() in any order. NetworkNode defers registration and applies it
/// retroactively when the participant is created.
///
/// @tparam Transport  Any ITransport-derived class with a nested Type enum
///                    providing Type::PUB (sender) and Type::SUB (receiver).
/// @tparam MaxPeers   Maximum remote peers (fixed allocation, default 4).
/// @tparam MaxTopics  Maximum in- or out-topics each (fixed, default 16).

#include "DataBus.h"
#include "extras/util/TransportMonitor.h"
#include "extras/util/RetryMonitor.h"
#include "extras/util/ReliableTransport.h"
#include "extras/util/Timer.h"
#include "extras/util/TimerDelegate.h"
#include "extras/util/Fault.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <chrono>

// Select the OS thread implementation based on the build configuration.
#if defined(DMQ_THREAD_STDLIB)
    #include "port/os/stdlib/Thread.h"
#elif defined(DMQ_THREAD_WIN32)
    #include "port/os/win32/Thread.h"
#elif defined(DMQ_THREAD_FREERTOS)
    #include "port/os/freertos/Thread.h"
#elif defined(DMQ_THREAD_THREADX)
    #include "port/os/threadx/Thread.h"
#elif defined(DMQ_THREAD_ZEPHYR)
    #include "port/os/zephyr/Thread.h"
#elif defined(DMQ_THREAD_CMSIS_RTOS2)
    #include "port/os/cmsis-rtos2/Thread.h"
#elif defined(DMQ_THREAD_QT)
    #include "port/os/qt/Thread.h"
#endif

namespace dmq::databus {

/// Reliability tier for outgoing topics registered via NetworkNode::Send().
enum class Reliability {
    UNRELIABLE, ///< Raw transport send — no ACK, no retry.
    RELIABLE,   ///< Transport send with ACK and automatic retry (RetryMonitor stack).
};

/// @brief Generic multi-peer DataBus network node.
///
/// All member functions are safe to call before Start() or AddPeer(); registrations
/// are stored and applied when the relevant participant is created.
template <typename Transport, size_t MaxPeers = 4, size_t MaxTopics = 16>
class NetworkNode {
    using TransportMonitor  = dmq::util::TransportMonitor;
    using RetryMonitor      = dmq::util::RetryMonitor;
    using ReliableTransport = dmq::util::ReliableTransport;

public:
    NetworkNode()  = default;
    ~NetworkNode() { Stop(); }

    NetworkNode(const NetworkNode&)            = delete;
    NetworkNode& operator=(const NetworkNode&) = delete;

    /// @brief Open the inbound transport and start the receive thread.
    ///
    /// @param nodeName    Human-readable name used for thread naming and logging.
    /// @param listenPort  Port this node receives all incoming traffic on.
    /// @param watchdog    Watchdog timeout for the receive thread (0 = disabled).
    /// @param tickPeriod  Interval between receive-loop polls.
    /// @return true on success.
    bool Start(const char* nodeName,
               uint16_t listenPort,
               dmq::Duration watchdog   = std::chrono::seconds(30),
               dmq::Duration tickPeriod = std::chrono::milliseconds(10))
    {
        if (m_running) return true;

        char threadName[64];
        snprintf(threadName, sizeof(threadName), "%s_NetworkThread", nodeName);

        if (m_recvTransport.Create(Transport::Type::SUB, "127.0.0.1", listenPort) != 0) {
            printf("NetworkNode [%s]: ERROR - failed to open recv port %u\n", nodeName, listenPort);
            return false;
        }
        m_recvTransport.SetRecvTimeout(std::chrono::milliseconds(1));
        // Allow the SUB socket to send ACKs back to senders of RELIABLE messages.
        // The `else if (m_sendTransport)` gate in Receive() requires a non-null
        // m_sendTransport; using self means ACKs go out via sendto on this socket.
        m_recvTransport.SetSendTransport(&m_recvTransport);

        m_recvParticipant = dmq::xmake_shared<Participant>(m_recvTransport);

        // Apply incoming topics registered before Start().
        for (size_t i = 0; i < m_inCount; ++i)
            m_inTopics[i].adder(m_inTopics[i].serializer,
                                m_inTopics[i].topic,
                                m_inTopics[i].remoteId,
                                *m_recvParticipant);

        m_thread = std::make_unique<dmq::os::Thread>(
            threadName, 100, dmq::os::FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT);

        m_running    = true;
        m_tickPeriod = tickPeriod;

        std::optional<dmq::Duration> wd = (watchdog.count() > 0)
            ? std::optional<dmq::Duration>{watchdog}
            : std::nullopt;
        m_thread->CreateThread(wd);

        m_recvConn = m_recvTimer.OnExpired.Connect(
            dmq::util::MakeTimerDelegate(this, &NetworkNode::ReceiverThread, *m_thread));
        m_recvTimer.Start(tickPeriod);

        return true;
    }

    /// @brief Stop all transports and exit the receive thread.
    void Stop()
    {
        if (!m_running) return;
        m_running = false;

        m_recvTimer.Stop();
        m_recvConn.Disconnect();
        m_recvTransport.Close();

        if (m_thread)
            m_thread->ExitThread();

        for (size_t i = m_peerCount; i > 0; --i) {
            RemoteNode& node = m_peers[i - 1];
            if (!node.active) continue;
            node.capConn.Disconnect();
            node.pendingConn.Disconnect();
            node.rawTransport.Close();
            node.active = false;
        }
        m_peerCount = 0;
    }

    /// @brief Add a remote peer.
    ///
    /// Outgoing topics already registered via Send() are applied immediately.
    /// May be called before or after Start().
    ///
    /// @param name  Peer name (for logging, up to 47 chars).
    /// @param addr  IP address of the peer.
    /// @param port  UDP port the peer listens on.
    void AddPeer(const char* name, const char* addr, uint16_t port)
    {
        ASSERT_TRUE(m_peerCount < MaxPeers);

        RemoteNode& node = m_peers[m_peerCount];
        strncpy(node.name, name, sizeof(node.name) - 1);
        node.name[sizeof(node.name) - 1] = '\0';

        if (node.rawTransport.Create(Transport::Type::PUB, addr, port) != 0) {
            printf("NetworkNode: ERROR - failed to connect to peer '%s' at %s:%u\n", name, addr, port);
            return;
        }

        // Wire the transport monitor so Send() calls TransportMonitor::Add(seqNum)
        // and incoming ACKs call TransportMonitor::Remove(seqNum). Without this,
        // m_pending stays empty, Process() never fires timeouts, and RetryMonitor
        // entries are never erased.
        node.rawTransport.SetTransportMonitor(&node.transportMonitor);

        // Allow Receive() to be called on the PUB socket so ReceiverThread can drain
        // ACKs. Remote peers send ACKs to our PUB socket's OS-assigned ephemeral port
        // (the source address they saw when the message arrived), not to our SUB listen
        // port. Without this, Remove() is never called and m_pending fills to cap.
        node.rawTransport.SetRecvTransport(&node.rawTransport);

        // Two-phase init: wire the reliability stack using stable member addresses.
        // RemoteNode is stored by value in std::array — elements never relocate.
        node.retryMonitor.Init(node.rawTransport, node.transportMonitor);
        node.reliableTransport.Init(node.rawTransport, node.retryMonitor);

        dmq::xstring peerName(name);
        node.capConn = node.transportMonitor.OnCapExceeded.Connect(
            dmq::MakeDelegate([peerName](size_t n) {
                printf("NetworkNode [%s]: cap exceeded (%zu unacked messages)\n",
                       peerName.c_str(), n);
            }));
        node.pendingConn = node.transportMonitor.OnPendingExceeded.Connect(
            dmq::MakeDelegate([peerName](size_t n) {
                printf("NetworkNode [%s]: pending exceeded (%zu entries remain)\n",
                       peerName.c_str(), n);
            }));

        node.reliableParticipant   = dmq::xmake_shared<Participant>(node.reliableTransport);
        node.unreliableParticipant = dmq::xmake_shared<Participant>(node.rawTransport);

        if (m_thread) {
            node.reliableParticipant->SetSendThread(m_thread.get());
            node.unreliableParticipant->SetSendThread(m_thread.get());
        }

        DataBus::AddParticipant(node.reliableParticipant);
        DataBus::AddParticipant(node.unreliableParticipant);

        // Apply outgoing topics registered before this peer was added.
        for (size_t i = 0; i < m_outCount; ++i) {
            const OutgoingTopic& out = m_outTopics[i];
            if (out.reliability == Reliability::RELIABLE)
                node.reliableParticipant->AddRemoteTopic(out.topic, out.remoteId);
            else
                node.unreliableParticipant->AddRemoteTopic(out.topic, out.remoteId);
        }

        node.active = true;
        m_peerCount++;
    }

    /// @brief Register a topic this node publishes to all peers.
    ///
    /// Registers the serializer with DataBus and maps the topic to the correct
    /// reliability-tier participant on every existing and future peer.
    ///
    /// @param topic      DataBus topic string.
    /// @param remoteId   Wire ID embedded in the serialized frame.
    /// @param serializer Serializer instance for type T.
    /// @param rel        Reliability tier (default UNRELIABLE).
    template <typename T>
    void Send(const dmq::xstring& topic,
              dmq::DelegateRemoteId remoteId,
              dmq::ISerializer<void(T)>& serializer,
              Reliability rel = Reliability::UNRELIABLE)
    {
        ASSERT_TRUE(m_outCount < MaxTopics);
        DataBus::RegisterSerializer<T>(topic, serializer);
        m_outTopics[m_outCount++] = {topic, remoteId, rel};

        // Apply to peers already added.
        for (size_t i = 0; i < m_peerCount; ++i) {
            RemoteNode& node = m_peers[i];
            if (!node.active) continue;
            if (rel == Reliability::RELIABLE)
                node.reliableParticipant->AddRemoteTopic(topic, remoteId);
            else
                node.unreliableParticipant->AddRemoteTopic(topic, remoteId);
        }
    }

    /// @brief Register a topic this node receives from the network.
    ///
    /// Registers an incoming topic on the receive-side participant. May be called
    /// before or after Start(); applied immediately if Start() has already run.
    ///
    /// @param topic      DataBus topic string.
    /// @param remoteId   Wire ID embedded in the serialized frame.
    /// @param serializer Serializer instance for type T.
    template <typename T>
    void Receive(const dmq::xstring& topic,
                 dmq::DelegateRemoteId remoteId,
                 dmq::ISerializer<void(T)>& serializer)
    {
        ASSERT_TRUE(m_inCount < MaxTopics);

        // Store a type-erased adder so it can be replayed when Start() creates the participant.
        m_inTopics[m_inCount++] = {
            topic, remoteId, static_cast<void*>(&serializer),
            [](void* ser, const dmq::xstring& t, dmq::DelegateRemoteId rid, Participant& p) {
                DataBus::AddIncomingTopic<T>(t, rid, p,
                    *static_cast<dmq::ISerializer<void(T)>*>(ser));
            }
        };

        // Apply immediately if Start() has already run.
        if (m_recvParticipant)
            DataBus::AddIncomingTopic<T>(topic, remoteId, *m_recvParticipant, serializer);
    }

private:
    void ReceiverThread()
    {
        constexpr int MAX_WORK = 20;

        if (m_recvParticipant) {
            for (int i = 0; i < MAX_WORK; ++i)
                if (m_recvParticipant->ProcessIncoming() != 0) break;
        }

        // Drain ACKs that arrived on each peer's PUB socket (ephemeral source port).
        // Remote peers send ACKs to the source address they received from, which is
        // our PUB socket's OS-assigned port — not our SUB listen port. Receive() on
        // the PUB socket calls TransportMonitor::Remove(seqNum) when an ACK arrives,
        // firing SUCCESS so RetryMonitor erases the entry. The 2 ms recv timeout on
        // PUB sockets keeps each poll fast when there is nothing to receive.
        for (size_t i = 0; i < m_peerCount; ++i) {
            if (!m_peers[i].active) continue;
            for (int k = 0; k < MAX_WORK; ++k) {
                dmq::transport::DmqHeader ackHeader;
                dmq::xstringstream ackStream(std::ios::in | std::ios::out | std::ios::binary);
                if (m_peers[i].rawTransport.Receive(ackStream, ackHeader) != 0) break;
            }
        }

        // Throttled: run TransportMonitor::Process() ~10 times per second.
        auto now = dmq::Clock::now();
        if (now - m_lastMonitorCheck >= std::chrono::milliseconds(100)) {
            for (size_t i = 0; i < m_peerCount; ++i) {
                if (m_peers[i].active)
                    m_peers[i].transportMonitor.Process();
            }
            m_lastMonitorCheck = now;
        }
    }

    // -----------------------------------------------------------------------
    // RemoteNode — fully by-value storage; no heap for transport objects.
    //
    // std::array guarantees elements are never relocated after construction,
    // so Init() references between sibling members remain valid for the
    // lifetime of this NetworkNode:
    //   retryMonitor    refs rawTransport  + transportMonitor
    //   reliableTransport refs rawTransport + retryMonitor
    // -----------------------------------------------------------------------
    struct RemoteNode {
        Transport         rawTransport;
        TransportMonitor  transportMonitor;
        RetryMonitor      retryMonitor;        // Init(rawTransport, transportMonitor)
        ReliableTransport reliableTransport;   // Init(rawTransport, retryMonitor)
        dmq::ScopedConnection capConn;
        dmq::ScopedConnection pendingConn;
        std::shared_ptr<Participant> reliableParticipant;    // xmake_shared on construction
        std::shared_ptr<Participant> unreliableParticipant;  // xmake_shared on construction
        bool active = false;
        char name[48] = {};
    };

    // Type-erased incoming-topic record. Avoids storing template arguments past
    // the Receive() call site while preserving type-safe AddIncomingTopic<T>.
    using TopicAdder = void(*)(void*, const dmq::xstring&, dmq::DelegateRemoteId, Participant&);

    struct IncomingTopic {
        dmq::xstring          topic;
        dmq::DelegateRemoteId remoteId   = 0;
        void*                 serializer = nullptr;
        TopicAdder            adder      = nullptr;
    };

    struct OutgoingTopic {
        dmq::xstring          topic;
        dmq::DelegateRemoteId remoteId    = 0;
        Reliability           reliability = Reliability::UNRELIABLE;
    };

    // Inbound path — one participant handles all incoming traffic on listenPort.
    Transport                    m_recvTransport;
    std::shared_ptr<Participant> m_recvParticipant;

    // Peer table — fixed-size; no heap for transport or reliability-stack objects.
    std::array<RemoteNode, MaxPeers> m_peers{};
    size_t m_peerCount = 0;

    // Topic tables — fixed-size.
    std::array<IncomingTopic, MaxTopics> m_inTopics{};
    size_t m_inCount = 0;
    std::array<OutgoingTopic, MaxTopics> m_outTopics{};
    size_t m_outCount = 0;

    // Receive thread. One heap allocation per NetworkNode lifetime — acceptable
    // because the thread name is set at runtime via Start(nodeName, ...).
    std::unique_ptr<dmq::os::Thread> m_thread;
    dmq::util::Timer                 m_recvTimer;
    dmq::ScopedConnection            m_recvConn;
    dmq::Duration                    m_tickPeriod{};
    dmq::TimePoint                   m_lastMonitorCheck{};
    bool                             m_running = false;
};

} // namespace dmq::databus

#endif // DMQ_NETWORK_NODE_H
