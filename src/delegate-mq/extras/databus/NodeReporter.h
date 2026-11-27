#ifndef DMQ_NODE_REPORTER_H
#define DMQ_NODE_REPORTER_H

/// @file NodeReporter.h
/// @brief Reports this node to DelegateMQ's diagnostic consoles through any
/// DelegateMQ transport: heartbeats and health counters to dmq-monitor, thread
/// statistics to dmq-thread. An embedded target with a network shows up in
/// both as itself.
///
/// @details What it sends, each as one DelegateMQ message (NodePackets.h):
/// - Every heartbeat period (DMQ_NODE_REPORTER_HEARTBEAT_MS, 1 s by default):
///   a NodeInfoPacket (name, address, uptime, DataBus message count, topics
///   seen) and a NodeHealthPacket (DataBus errors, and the link problems of
///   whatever Watch() was given: ACK timeouts, abandoned messages, backlog
///   limits, with the most recent as text).
/// - With DMQ_DATABUS_TOOLS: every dmq::util::ThreadStatsPacket that
///   dmq::util::ThreadMonitor publishes (queue depth, dispatch latency per
///   registered thread). Register threads with ThreadMonitor::Register() and
///   call ThreadMonitor::Enable(); without DMQ_DATABUS_TOOLS there are none.
///
/// Usage:
/// @code
/// dmq::transport::UdpTransport transport;   // the port's UDP transport
/// transport.Create(dmq::transport::UdpTransport::Type::PUB, "239.1.1.1", 9998);
/// static dmq::databus::NodeReporter reporter;
/// reporter.Watch(networkNode);               // optional: its link health
/// reporter.Start(transport, "Controller", "192.168.0.241");
/// @endcode
/// dmq-monitor and dmq-thread share port 9998, each listening on the multicast
/// group 239.1.1.1: send to the group, not to one PC address, or only one of
/// the two receives it.
///
/// Costs: the DataBus message count and topic list come from
/// DataBus::Monitor(), so while a reporter (or any Monitor() subscriber) is
/// attached each publish stringifies its value; the reporter's part runs on
/// the publishing thread and is a counter and a set lookup under a mutex.
/// Sending happens on the reporter's own thread. Health counters are updated
/// on whichever thread reports the problem.
///
/// Never stalls the application: the reporter's queue (heartbeat ticks,
/// thread statistics) drops when full (FullPolicy::DROP), and a failed send is
/// dropped too. Every drop is counted (GetDropped()) and reported (OnDropped).
/// Messages are UNRELIABLE: no ACK, no retry. A lost heartbeat loses nothing,
/// since the counters are totals.
///
/// One transport per reporter, used only by the reporter: Send() runs on the
/// reporter's thread.

#include "delegate/DelegateOpt.h"
#include "extras/databus/DataBus.h"
#include "extras/databus/NodePackets.h"
#include "extras/util/Timer.h"
#include "extras/util/TransportMonitor.h"
#include "extras/util/RetryMonitor.h"
#if defined(DMQ_DATABUS_TOOLS)
#include "extras/util/ThreadMonitor.h"
#include "extras/util/ThreadMonitorSer.h"
#endif
#include "port/transport/common/ITransport.h"
#include "port/serialize/serialize/msg_serialize.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <optional>

#if !defined(DMQ_HAS_THREADING)
    #error "NodeReporter sends on its own thread: it needs a thread port (DMQ_THREAD_*)"
#endif

namespace dmq::databus {

class NodeReporter {
public:
    /// Signal connections Watch() can hold: a NetworkNode takes 4, a
    /// TransportMonitor 3, a RetryMonitor 1. Exceeding it faults.
    static constexpr size_t MAX_WATCHES = 16;

    /// @param queueSize Heartbeat ticks and thread statistics that may wait to
    ///        be sent; beyond it new ones are dropped (default
    ///        DMQ_NODE_REPORTER_QUEUE_SIZE).
    explicit NodeReporter(size_t queueSize = dmq::NODE_REPORTER_QUEUE_SIZE)
        : m_thread("NodeReporter", queueSize, dmq::os::FullPolicy::DROP)
    {
        m_thread.SetDroppedHandler(dmq::MakeDelegate(this, &NodeReporter::OnQueueFull));
    }

    ~NodeReporter() { Stop(); }

    NodeReporter(const NodeReporter&) = delete;
    NodeReporter& operator=(const NodeReporter&) = delete;

    /// @brief The reporter's thread, to set its priority, stack or core
    ///        (SetCoreAffinity()) before Start().
    dmq::os::Thread& GetThread() { return m_thread; }

    /// @brief Start reporting through transport.
    /// @param transport A transport that sends to the consoles (e.g. a PUB UDP
    ///        transport to 239.1.1.1:9998). Must outlive the reporter, or
    ///        Stop(); only the reporter's thread sends on it.
    /// @param nodeId This node's name in dmq-monitor and dmq-thread.
    /// @param ipAddress This node's address as text, shown by dmq-monitor and
    ///        part of its row key (with nodeId).
    /// @param hostname Shown by dmq-monitor; "" if the target has none.
    /// @param heartbeat Heartbeat period (default DMQ_NODE_REPORTER_HEARTBEAT_MS).
    ///        Needs Timer::ProcessTimers() running, as every Timer does.
    /// @param watchdog Optional watchdog timeout for the reporter's thread;
    ///        none by default. With one, a thread stuck in Send() faults like
    ///        any other hung thread (Thread::WatchdogCheckAll()).
    /// @return false if the thread could not be created.
    bool Start(dmq::transport::ITransport& transport, const char* nodeId, const char* ipAddress,
               const char* hostname = "", dmq::Duration heartbeat = dmq::NODE_REPORTER_HEARTBEAT,
               std::optional<dmq::Duration> watchdog = std::nullopt)
    {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_running)
            return true;
        m_transport = &transport;
        m_nodeId = nodeId ? nodeId : "";
        m_ipAddress = ipAddress ? ipAddress : "";
        m_hostname = hostname ? hostname : "";
        m_start = dmq::Clock::now();
        m_msgCount = 0;
        m_dataBusErrors = m_sendTimeouts = m_deliveryFailures = m_capExceeded = m_pendingExceeded = 0;
        {
            dmq::LockGuard<dmq::Mutex> dataLock(m_dataMutex);
            m_topics.clear();
            m_lastEvent[0] = '\0';
        }

        if (!m_thread.CreateThread(watchdog))
            return false;

        // Message count and topics: on the publishing thread (no queueing)
        m_monitorConn = DataBus::Monitor(dmq::MakeDelegate(this, &NodeReporter::OnMonitor));
        // DataBus errors count toward health on every node, with no app code
        m_dataBusErrorConn = DataBus::SubscribeError(dmq::MakeDelegate(this, &NodeReporter::OnDataBusError));
#if defined(DMQ_DATABUS_TOOLS)
        // ThreadMonitor's statistics, forwarded on the reporter's thread. The
        // stringifier gives them readable values in dmq-spy too.
        DataBus::RegisterSerializer<dmq::util::ThreadStatsPacket>(THREAD_STATS_TOPIC, m_threadStatsSer);
        DataBus::RegisterStringifier<dmq::util::ThreadStatsPacket>(THREAD_STATS_TOPIC,
            dmq::MakeDelegate(&dmq::util::ThreadStatsPacketToString));
        m_threadStatsConn = DataBus::Subscribe<dmq::util::ThreadStatsPacket>(THREAD_STATS_TOPIC,
            dmq::MakeDelegate(this, &NodeReporter::OnThreadStats), &m_thread);
#endif
        m_timerConn = m_heartbeatTimer.OnExpired.Connect(
            dmq::MakeDelegate(this, &NodeReporter::OnHeartbeat, m_thread));
        m_heartbeatTimer.Start(heartbeat);
        m_running = true;
        return true;
    }

    /// @brief Stop reporting: unsubscribe, drop all Watch() connections and
    ///        exit the thread. Start() may be called again afterwards.
    void Stop()
    {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        m_heartbeatTimer.Stop();
        m_timerConn.Disconnect();
        m_monitorConn.Disconnect();
        m_dataBusErrorConn.Disconnect();
#if defined(DMQ_DATABUS_TOOLS)
        m_threadStatsConn.Disconnect();
#endif
        for (size_t i = 0; i < m_watchCount; ++i)
            m_watchConns[i].Disconnect();
        m_watchCount = 0;
        AddSources(0, true);
        if (m_running) {
            m_thread.ExitThread();
            m_running = false;
        }
    }

    /// @brief Report a TransportMonitor's ACK timeouts and limits in the health packet.
    /// @note May be called before or after Start(). The monitor must outlive
    ///       the reporter, or Stop().
    void Watch(dmq::util::TransportMonitor& monitor)
    {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        AddSources(NodeHealthPacket::SRC_TRANSPORT);
        AddWatch(monitor.OnSendStatus.Connect(dmq::MakeDelegate(this, &NodeReporter::OnSendStatus)));
        AddWatch(monitor.OnCapExceeded.Connect(dmq::MakeDelegate(this, &NodeReporter::OnCapExceeded)));
        AddWatch(monitor.OnPendingExceeded.Connect(dmq::MakeDelegate(this, &NodeReporter::OnPendingExceeded)));
    }

    /// @brief Report a RetryMonitor's abandoned messages in the health packet.
    /// @note As Watch(TransportMonitor&).
    void Watch(dmq::util::RetryMonitor& monitor)
    {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        AddSources(NodeHealthPacket::SRC_RETRY);
        AddWatch(monitor.OnDeliveryFailed.Connect(dmq::MakeDelegate(this, &NodeReporter::OnDeliveryFailed)));
    }

    /// @brief Report a dmq::databus::NetworkNode's link health, for all its peers.
    /// @details A template so this header doesn't need NetworkNode.h.
    /// @note As Watch(TransportMonitor&).
    template <typename NetworkNodeT>
    void Watch(NetworkNodeT& node)
    {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        AddSources(NodeHealthPacket::SRC_TRANSPORT | NodeHealthPacket::SRC_RETRY);
        AddWatch(node.OnPeerSendStatus.Connect(dmq::MakeDelegate(this, &NodeReporter::OnPeerSendStatus)));
        AddWatch(node.OnDeliveryFailed.Connect(dmq::MakeDelegate(this, &NodeReporter::OnPeerDeliveryFailed)));
        AddWatch(node.OnPeerCapExceeded.Connect(dmq::MakeDelegate(this, &NodeReporter::OnPeerCapExceeded)));
        AddWatch(node.OnPeerPendingExceeded.Connect(dmq::MakeDelegate(this, &NodeReporter::OnPeerPendingExceeded)));
    }

    /// @brief Messages sent so far.
    uint32_t GetSent() const { return m_sent.load(); }

    /// @brief Messages dropped so far: queue full, serialization or send failed.
    uint32_t GetDropped() const { return m_dropped.load(); }

    /// @brief Fired on every dropped message with the total dropped so far.
    /// @note Fires on the thread that queued (queue full) or on the reporter's
    ///       thread (send failed). Keep handlers short; rate-limit any logging.
    dmq::Signal<void(uint32_t)> OnDropped;

    /// DataBus topic ThreadMonitor publishes on (its default)
    static constexpr const char* THREAD_STATS_TOPIC = "ThreadStats";

private:
    using Status = dmq::util::TransportMonitor::Status;
    static constexpr size_t LAST_EVENT_SIZE = 96;

    // --- Publishing thread ---------------------------------------------------

    void OnMonitor(const SpyPacket& packet)
    {
        m_msgCount.fetch_add(1);
        dmq::LockGuard<dmq::Mutex> lock(m_dataMutex);
        if (m_topics.find(packet.topic) == m_topics.end())
            m_topics.insert(packet.topic);
    }

    // --- Reporter thread -----------------------------------------------------

    void OnHeartbeat()
    {
        NodeInfoPacket info;
        info.nodeId = m_nodeId;
        info.hostname = m_hostname;
        info.ipAddress = m_ipAddress;
        info.totalMsgCount = m_msgCount.load();
        info.uptimeMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(dmq::Clock::now() - m_start).count());
        {
            dmq::LockGuard<dmq::Mutex> lock(m_dataMutex);
            for (const auto& topic : m_topics) {
                if (!info.topicsStr.empty())
                    info.topicsStr += ';';
                info.topicsStr += topic;
            }
        }
        m_writer.reset();
        m_serialize.write(m_writer, info);
        Send(NODE_INFO_REMOTE_ID);

        NodeHealthPacket health;
        health.nodeId = m_nodeId;
        health.ipAddress = m_ipAddress;
        health.dataBusErrors = m_dataBusErrors.load();
        health.sendTimeouts = m_sendTimeouts.load();
        health.deliveryFailures = m_deliveryFailures.load();
        health.capExceeded = m_capExceeded.load();
        health.pendingExceeded = m_pendingExceeded.load();
        {
            dmq::LockGuard<dmq::Mutex> lock(m_dataMutex);
            health.sources = m_sources;
            health.lastEvent = m_lastEvent;
        }
        m_writer.reset();
        m_serialize.write(m_writer, health);
        Send(NODE_HEALTH_REMOTE_ID);
    }

#if defined(DMQ_DATABUS_TOOLS)
    void OnThreadStats(const dmq::util::ThreadStatsPacket& packet)
    {
        m_writer.reset();
        if (packet.cpu_name.empty() && !m_nodeId.empty()) {
            // A thread without a CPU name (the Thread constructor's cpuName,
            // and every thread the library creates itself: NetworkNode, the
            // reporters) is grouped under this node in dmq-thread
            dmq::util::ThreadStatsPacket named = packet;
            named.cpu_name = m_nodeId;
            m_threadStatsSer.Write(m_writer, named);
        } else {
            m_threadStatsSer.Write(m_writer, packet);
        }
        Send(THREAD_STATS_REMOTE_ID);
    }
#endif

    // m_writer holds the serialized packet
    void Send(uint16_t remoteId)
    {
        if (!m_writer.good()) {
            Drop();
            return;
        }
        dmq::transport::DmqHeader header;
        header.SetId(remoteId);
        header.SetSeqNum(m_seqNum++);
        if (m_transport->Send(m_writer, header) != 0) {
            Drop();
            return;
        }
        m_sent.fetch_add(1);
    }

    // --- Any thread ----------------------------------------------------------

    void OnQueueFull(size_t /*queueDepth*/) { Drop(); }
    void Drop() { OnDropped(m_dropped.fetch_add(1) + 1); }

    // What the health packet reports on (NodeHealthPacket::SRC_*)
    void AddSources(uint32_t bits, bool reset = false)
    {
        dmq::LockGuard<dmq::Mutex> lock(m_dataMutex);
        if (reset)
            m_sources = NodeHealthPacket::SRC_DATABUS;
        m_sources |= bits;
    }

    void AddWatch(dmq::ScopedConnection&& conn)
    {
        DMQ_ASSERT_TRUE(m_watchCount < MAX_WATCHES);    // see MAX_WATCHES
        m_watchConns[m_watchCount++] = std::move(conn);
    }

    void OnDataBusError(const dmq::xstring& topic, dmq::DelegateError error)
    {
        m_dataBusErrors.fetch_add(1);
        SetLastEvent("DataBus error %d on %s", static_cast<int>(error), topic.c_str());
    }

    void OnSendStatus(dmq::DelegateRemoteId id, uint16_t seq, Status status)
    {
        if (status == Status::TIMEOUT)
            RecordTimeout("", id, seq);
    }
    void OnPeerSendStatus(const dmq::xstring& peer, dmq::DelegateRemoteId id, uint16_t seq, Status status)
    {
        if (status == Status::TIMEOUT)
            RecordTimeout(peer.c_str(), id, seq);
    }
    void OnDeliveryFailed(dmq::DelegateRemoteId id, uint16_t seq) { RecordDeliveryFailure("", id, seq); }
    void OnPeerDeliveryFailed(const dmq::xstring& peer, dmq::DelegateRemoteId id, uint16_t seq)
    {
        RecordDeliveryFailure(peer.c_str(), id, seq);
    }
    void OnCapExceeded(size_t count) { RecordCapExceeded("", count); }
    void OnPeerCapExceeded(const dmq::xstring& peer, size_t count) { RecordCapExceeded(peer.c_str(), count); }
    void OnPendingExceeded(size_t remaining) { RecordPendingExceeded("", remaining); }
    void OnPeerPendingExceeded(const dmq::xstring& peer, size_t remaining)
    {
        RecordPendingExceeded(peer.c_str(), remaining);
    }

    void RecordTimeout(const char* peer, dmq::DelegateRemoteId id, uint16_t seq)
    {
        m_sendTimeouts.fetch_add(1);
        SetLastEvent("ACK timeout,%s%s id=%u seq=%u, retrying", *peer ? " " : "", peer,
                     static_cast<unsigned>(id), static_cast<unsigned>(seq));
    }
    void RecordDeliveryFailure(const char* peer, dmq::DelegateRemoteId id, uint16_t seq)
    {
        m_deliveryFailures.fetch_add(1);
        SetLastEvent("delivery failed,%s%s id=%u seq=%u, retries exhausted", *peer ? " " : "", peer,
                     static_cast<unsigned>(id), static_cast<unsigned>(seq));
    }
    void RecordCapExceeded(const char* peer, size_t count)
    {
        m_capExceeded.fetch_add(1);
        SetLastEvent("send rejected,%s%s %u messages unacknowledged", *peer ? " " : "", peer,
                     static_cast<unsigned>(count));
    }
    void RecordPendingExceeded(const char* peer, size_t remaining)
    {
        m_pendingExceeded.fetch_add(1);
        SetLastEvent("retry backlog,%s%s %u expired messages waiting", *peer ? " " : "", peer,
                     static_cast<unsigned>(remaining));
    }

    template <typename... Args>
    void SetLastEvent(const char* format, Args... args)
    {
        dmq::LockGuard<dmq::Mutex> lock(m_dataMutex);
        snprintf(m_lastEvent, sizeof(m_lastEvent), format, args...);
    }

    dmq::os::Thread m_thread;
    dmq::util::Timer m_heartbeatTimer;
    dmq::ScopedConnection m_timerConn;
    dmq::ScopedConnection m_monitorConn;
    dmq::ScopedConnection m_dataBusErrorConn;
#if defined(DMQ_DATABUS_TOOLS)
    dmq::ScopedConnection m_threadStatsConn;
    dmq::util::ThreadStatsPacketSerializer m_threadStatsSer;
#endif
    std::array<dmq::ScopedConnection, MAX_WATCHES> m_watchConns;
    size_t m_watchCount = 0;                            // m_mutex
    dmq::transport::ITransport* m_transport = nullptr;
    dmq::xstring m_nodeId;
    dmq::xstring m_ipAddress;
    dmq::xstring m_hostname;
    dmq::TimePoint m_start{};

    // Reporter thread only
    ::serialize m_serialize;
    dmq::ByteWriter m_writer;
    uint16_t m_seqNum = 0;

    // Any thread
    std::atomic<uint32_t> m_msgCount{0};
    std::atomic<uint32_t> m_dataBusErrors{0};
    std::atomic<uint32_t> m_sendTimeouts{0};
    std::atomic<uint32_t> m_deliveryFailures{0};
    std::atomic<uint32_t> m_capExceeded{0};
    std::atomic<uint32_t> m_pendingExceeded{0};
    std::atomic<uint32_t> m_sent{0};
    std::atomic<uint32_t> m_dropped{0};

    dmq::Mutex m_dataMutex;                             // m_topics, m_sources, m_lastEvent
    dmq::xset<dmq::xstring> m_topics;
    uint32_t m_sources = NodeHealthPacket::SRC_DATABUS;
    char m_lastEvent[LAST_EVENT_SIZE] = {};

    bool m_running = false;                             // m_mutex
    dmq::RecursiveMutex m_mutex;                        // Start/Stop/Watch
};

} // namespace dmq::databus

#endif // DMQ_NODE_REPORTER_H
