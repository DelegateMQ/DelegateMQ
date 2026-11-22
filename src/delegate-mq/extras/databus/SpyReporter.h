#ifndef DMQ_SPY_REPORTER_H
#define DMQ_SPY_REPORTER_H

/// @file SpyReporter.h
/// @brief Sends this node's DataBus traffic to dmq-spy through any DelegateMQ
/// transport, so an embedded target shows up in the spy as itself, with its
/// own clock, next to the desktop nodes.
///
/// @details Every message published on (or received into) this node's DataBus
/// reaches DataBus::Monitor() as a SpyPacket: topic, stringified value
/// (DataBus::RegisterStringifier(), "?" without one) and timestamp. SpyReporter
/// queues each one to its own thread, adds this node's name and sends it as
/// one message with remote ID SPY_REMOTE_ID through the transport given to
/// Start() -- typically a UDP transport (the port's UdpTransport, Type::PUB)
/// aimed at dmq-spy's port (9999 by default). The desktop tools' SpyBridge
/// (tools/bridge/spy) is a front end to this class.
///
/// Usage:
/// @code
/// dmq::transport::UdpTransport spyTransport;
/// spyTransport.Create(dmq::transport::UdpTransport::Type::PUB, "192.168.0.123", 9999);
/// static dmq::databus::SpyReporter spy;
/// spy.GetThread().SetCoreAffinity(1u << 0);    // optional, before Start()
/// spy.Start(spyTransport, "Controller");
/// @endcode
///
/// Costs: while a bridge (any DataBus::Monitor() subscriber) is attached, each
/// publish stringifies its value on the publishing thread, then queues a copy
/// to the bridge's thread, which serializes and sends it. Nothing is ever
/// sent from the publishing thread.
///
/// Never stalls the application: the bridge's queue drops when full
/// (FullPolicy::DROP), and a failed send is dropped too. Every drop is counted
/// (GetDropped()) and reported (OnDropped). Packets are UNRELIABLE: no ACK,
/// no retry.
///
/// One transport per bridge, used only by the bridge: Send() runs on the
/// bridge's thread.

#include "delegate/DelegateOpt.h"
#include "extras/databus/DataBus.h"
#include "extras/databus/SpyPacket.h"
#include "port/transport/common/ITransport.h"
#include "port/serialize/serialize/msg_serialize.h"

#include <atomic>
#include <cstdint>
#include <optional>

#if !defined(DMQ_HAS_THREADING)
    #error "SpyReporter sends on its own thread: it needs a thread port (DMQ_THREAD_*)"
#endif

namespace dmq::databus {

class SpyReporter {
public:
    /// @param queueSize Messages that may wait to be sent; beyond it new ones
    ///        are dropped (default DMQ_SPY_REPORTER_QUEUE_SIZE).
    explicit SpyReporter(size_t queueSize = dmq::SPY_REPORTER_QUEUE_SIZE)
        : m_thread("SpyReporter", queueSize, dmq::os::FullPolicy::DROP)
    {
        m_thread.SetDroppedHandler(dmq::MakeDelegate(this, &SpyReporter::OnQueueFull));
    }

    ~SpyReporter() { Stop(); }

    SpyReporter(const SpyReporter&) = delete;
    SpyReporter& operator=(const SpyReporter&) = delete;

    /// @brief The bridge's thread, to set its priority, stack or core
    ///        (SetCoreAffinity()) before Start().
    dmq::os::Thread& GetThread() { return m_thread; }

    /// @brief Start sending this node's DataBus traffic through transport.
    /// @param transport A transport that sends to dmq-spy (e.g. a PUB UDP
    ///        transport to the PC's port 9999). Must outlive the bridge, or
    ///        Stop(); only the bridge's thread sends on it.
    /// @param nodeId This node's name, shown as the sender in dmq-spy.
    /// @param watchdog Optional watchdog timeout for the bridge's thread
    ///        (Thread::CreateThread()); none by default.
    /// @note With a watchdog, a bridge thread stuck in Send() (a transport that
    ///       blocks) for longer than the timeout is a hung thread, handled like
    ///       any other: Thread::WatchdogCheckAll() faults. Leave it off if a
    ///       diagnostic must never stop the system.
    /// @return false if the thread could not be created.
    bool Start(dmq::transport::ITransport& transport, const char* nodeId,
               std::optional<dmq::Duration> watchdog = std::nullopt)
    {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_running)
            return true;
        m_transport = &transport;
        m_nodeId = nodeId ? nodeId : "";
        if (!m_thread.CreateThread(watchdog))
            return false;
        m_monitorConn = DataBus::Monitor(dmq::MakeDelegate(this, &SpyReporter::OnPacket), &m_thread);
        m_running = true;
        return true;
    }

    /// @brief Stop sending: unsubscribe and exit the thread (queued packets
    ///        are discarded). Start() may be called again afterwards.
    void Stop()
    {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        if (!m_running)
            return;
        m_monitorConn.Disconnect();
        m_thread.ExitThread();
        m_running = false;
    }

    /// @brief Packets sent so far.
    uint32_t GetSent() const { return m_sent.load(); }

    /// @brief Packets dropped so far: queue full, or the send failed.
    uint32_t GetDropped() const { return m_dropped.load(); }

    /// @brief Fired on every dropped packet with the total dropped so far.
    /// @note Fires on the publishing thread when the queue is full, on the
    ///       bridge's thread when a send fails. Keep handlers short; rate-limit
    ///       any logging.
    dmq::Signal<void(uint32_t)> OnDropped;

private:
    // Bridge thread: add the node name, serialize, send as one message
    void OnPacket(const SpyPacket& packet)
    {
        SpyPacket out = packet;
        out.nodeId = m_nodeId;

        m_writer.reset();
        m_serialize.write(m_writer, out);
        if (!m_writer.good()) {
            Drop();
            return;
        }

        dmq::transport::DmqHeader header;
        header.SetId(SPY_REMOTE_ID);
        header.SetSeqNum(m_seqNum++);
        if (m_transport->Send(m_writer, header) != 0) {
            Drop();
            return;
        }
        m_sent.fetch_add(1);
    }

    // Publishing thread: the bridge's queue was full (FullPolicy::DROP)
    void OnQueueFull(size_t /*queueDepth*/) { Drop(); }

    void Drop() { OnDropped(m_dropped.fetch_add(1) + 1); }

    dmq::os::Thread m_thread;
    dmq::ScopedConnection m_monitorConn;
    dmq::transport::ITransport* m_transport = nullptr;
    dmq::xstring m_nodeId;
    ::serialize m_serialize;         // bridge thread only
    dmq::ByteWriter m_writer;        // bridge thread only; keeps its capacity
    uint16_t m_seqNum = 0;           // bridge thread only
    std::atomic<uint32_t> m_sent{0};
    std::atomic<uint32_t> m_dropped{0};
    bool m_running = false;
    dmq::RecursiveMutex m_mutex;
};

} // namespace dmq::databus

#endif // DMQ_SPY_REPORTER_H
