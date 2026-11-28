// Unit tests for dmq::databus::NodeReporter: what it sends to dmq-monitor and
// dmq-thread, through a transport that records every message.
// Heartbeats need Timer::ProcessTimers(), which test/main.cpp runs.

#include "DelegateMQ.h"
#include <iostream>

#if defined(DMQ_DATABUS)

#include "extras/databus/NodeReporter.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace dmq;
using namespace dmq::databus;

namespace {

/// Records what NodeReporter sends; Fail(true) makes Send() return an error.
class ReporterCaptureTransport : public dmq::transport::ITransport {
public:
    int Send(const dmq::ByteWriter& os, const dmq::transport::DmqHeader& header) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ids.push_back(header.GetId());
        m_payloads.emplace_back(os.data(), os.size());
        m_cv.notify_all();
        return m_fail ? -1 : 0;
    }

    int Receive(dmq::ByteReader& is, dmq::transport::DmqHeader&) override {
        is.assign(nullptr, 0);
        return -1;
    }

    void Fail(bool fail) { std::lock_guard<std::mutex> lock(m_mutex); m_fail = fail; }

    size_t Count() { std::lock_guard<std::mutex> lock(m_mutex); return m_payloads.size(); }

    /// Wait for a message with this remote ID that pred accepts, decoded as Packet
    template <typename Packet, typename Read, typename Pred>
    bool WaitFor(uint16_t id, Read read, Pred pred, Packet& out) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::seconds(3), [&] {
            for (size_t i = m_payloads.size(); i-- > 0;) {
                if (m_ids[i] != id) continue;
                Packet p;
                dmq::ByteReader is(m_payloads[i].data(), m_payloads[i].size());
                read(is, p);
                if (is.good() && pred(p)) { out = p; return true; }
            }
            return false;
        });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<uint16_t> m_ids;
    std::vector<std::string> m_payloads;
    bool m_fail = false;
};

void ReadSerialize(dmq::ByteReader& is, serialize::I& packet) {
    serialize ms;
    ms.read(is, packet);
}

constexpr auto HEARTBEAT = std::chrono::milliseconds(50);

} // namespace

int NodeReporterTestMain() {
    std::cout << "Starting NodeReporterTest..." << std::endl;

    // 1. Heartbeat: node info (name, address, hostname, messages, topics) and
    //    health, each as its own message with its own remote ID
    {
        DataBus::ResetForTesting();
        ReporterCaptureTransport transport;
        NodeReporter reporter;
        DMQ_ASSERT_TRUE(reporter.Start(transport, "NodeA", "10.0.0.1", "host-a", HEARTBEAT));

        DataBus::Publish<int>("nr/one", 1);
        DataBus::Publish<int>("nr/two", 2);

        NodeInfoPacket info;
        DMQ_ASSERT_TRUE(transport.WaitFor(NODE_INFO_REMOTE_ID,
            [](dmq::ByteReader& is, NodeInfoPacket& p) { ReadSerialize(is, p); },
            [](const NodeInfoPacket& p) { return p.totalMsgCount >= 2; }, info));
        DMQ_ASSERT_TRUE(info.nodeId == "NodeA");
        DMQ_ASSERT_TRUE(info.ipAddress == "10.0.0.1");
        DMQ_ASSERT_TRUE(info.hostname == "host-a");
        DMQ_ASSERT_TRUE(info.topicsStr.find("nr/one") != dmq::xstring::npos);
        DMQ_ASSERT_TRUE(info.topicsStr.find("nr/two") != dmq::xstring::npos);

        NodeHealthPacket health;
        DMQ_ASSERT_TRUE(transport.WaitFor(NODE_HEALTH_REMOTE_ID,
            [](dmq::ByteReader& is, NodeHealthPacket& p) { ReadSerialize(is, p); },
            [](const NodeHealthPacket&) { return true; }, health));
        DMQ_ASSERT_TRUE(health.nodeId == "NodeA");
        // Nothing watched: DataBus errors, plus the allocator's pools in a
        // DMQ_ALLOCATOR build (heap-backed here: a total, no fixed limit)
#if defined(DMQ_ALLOCATOR)
        DMQ_ASSERT_TRUE(health.sources == (NodeHealthPacket::SRC_DATABUS | NodeHealthPacket::SRC_ALLOCATOR));
        DMQ_ASSERT_TRUE(health.poolPeakBytes > 0);   // the reporter's own allocations, at least
#if !defined(DMQ_XALLOCATOR_STATIC_POOLS)
        DMQ_ASSERT_TRUE(health.poolCapacity == 0);
#else
        DMQ_ASSERT_TRUE(health.poolCapacity > 0 && health.poolBlockSize > 0);
        DMQ_ASSERT_TRUE(health.poolPeak <= health.poolCapacity);
#endif
#else
        DMQ_ASSERT_TRUE(health.sources == NodeHealthPacket::SRC_DATABUS);
        DMQ_ASSERT_TRUE(health.poolPeakBytes == 0 && health.poolCapacity == 0);
#endif
        DMQ_ASSERT_TRUE(health.sendTimeouts == 0);
        DMQ_ASSERT_TRUE(reporter.GetDropped() == 0);

        // Stopped: nothing more goes out
        reporter.Stop();
        const size_t count = transport.Count();
        std::this_thread::sleep_for(HEARTBEAT * 4);
        DMQ_ASSERT_TRUE(transport.Count() == count);
    }

    // 2. Watch(): a watched TransportMonitor's ACK timeout and a RetryMonitor's
    //    abandoned message show up in the health packet, with the last event
    {
        DataBus::ResetForTesting();
        ReporterCaptureTransport transport;
        dmq::util::TransportMonitor monitor(std::chrono::milliseconds(100));
        NodeReporter reporter;
        reporter.Watch(monitor);                // before Start(): allowed
        DMQ_ASSERT_TRUE(reporter.Start(transport, "NodeB", "10.0.0.2", "", HEARTBEAT));

        monitor.OnSendStatus(7, 42, dmq::util::TransportMonitor::Status::TIMEOUT);

        NodeHealthPacket health;
        DMQ_ASSERT_TRUE(transport.WaitFor(NODE_HEALTH_REMOTE_ID,
            [](dmq::ByteReader& is, NodeHealthPacket& p) { ReadSerialize(is, p); },
            [](const NodeHealthPacket& p) { return p.sendTimeouts == 1; }, health));
        DMQ_ASSERT_TRUE((health.sources & NodeHealthPacket::SRC_TRANSPORT) != 0);
        DMQ_ASSERT_TRUE(health.lastEvent.find("ACK timeout") != dmq::xstring::npos);
        DMQ_ASSERT_TRUE(health.lastEvent.find("id=7 seq=42") != dmq::xstring::npos);
        reporter.Stop();
    }

#if defined(DMQ_DATABUS_TOOLS)
    // 3. ThreadMonitor's statistics (published on "ThreadStats") are forwarded
    //    to dmq-thread
    {
        DataBus::ResetForTesting();
        ReporterCaptureTransport transport;
        NodeReporter reporter;
        DMQ_ASSERT_TRUE(reporter.Start(transport, "NodeC", "10.0.0.3", "", HEARTBEAT));

        dmq::util::ThreadStatsPacket stats;
        stats.thread_name = "Pump";
        stats.queue_depth = 3;
        stats.stack_size = 2048;
        stats.stack_free_min = 300;
        DataBus::Publish(NodeReporter::THREAD_STATS_TOPIC, stats);

        dmq::util::ThreadStatsPacket received;
        DMQ_ASSERT_TRUE(transport.WaitFor(THREAD_STATS_REMOTE_ID,
            [](dmq::ByteReader& is, dmq::util::ThreadStatsPacket& p) {
                dmq::util::ThreadStatsPacketSerializer ser;
                ser.Read(is, p);
            },
            [](const dmq::util::ThreadStatsPacket& p) { return p.thread_name == "Pump"; }, received));
        DMQ_ASSERT_TRUE(received.queue_depth == 3);
        DMQ_ASSERT_TRUE(received.stack_size == 2048);
        DMQ_ASSERT_TRUE(received.stack_free_min == 300);
        // No CPU name: grouped under the node's name
        DMQ_ASSERT_TRUE(received.cpu_name == "NodeC");

        // A thread that names its CPU keeps it
        dmq::util::ThreadStatsPacket named;
        named.thread_name = "Safety";
        named.cpu_name = "M4";
        DataBus::Publish(NodeReporter::THREAD_STATS_TOPIC, named);
        DMQ_ASSERT_TRUE(transport.WaitFor(THREAD_STATS_REMOTE_ID,
            [](dmq::ByteReader& is, dmq::util::ThreadStatsPacket& p) {
                dmq::util::ThreadStatsPacketSerializer ser;
                ser.Read(is, p);
            },
            [](const dmq::util::ThreadStatsPacket& p) { return p.thread_name == "Safety"; }, received));
        DMQ_ASSERT_TRUE(received.cpu_name == "M4");
        reporter.Stop();
    }
#endif

    // 3b. The health packet's pool fields survive the wire
    {
        NodeHealthPacket out;
        out.nodeId = "Pools";
        out.sources = NodeHealthPacket::SRC_DATABUS | NodeHealthPacket::SRC_ALLOCATOR;
        out.lastEvent = "x";
        out.poolBlockSize = 64;
        out.poolPeak = 37;
        out.poolCapacity = 48;
        out.poolPeakBytes = 5120;
        dmq::ByteWriter os;
        serialize ms;
        ms.write(os, out);
        dmq::ByteReader is(os);
        NodeHealthPacket in;
        ms.read(is, in);
        DMQ_ASSERT_TRUE(!is.fail() && is.remaining() == 0);
        DMQ_ASSERT_TRUE(in.nodeId == "Pools" && in.lastEvent == "x" && in.sources == out.sources);
        DMQ_ASSERT_TRUE(in.poolBlockSize == 64 && in.poolPeak == 37 && in.poolCapacity == 48 &&
                        in.poolPeakBytes == 5120);

        // A node built before the pool fields: its packet ends after lastEvent.
        // It still decodes, with the pool fields 0
        dmq::ByteWriter oldOs;
        ms.write(oldOs, out.nodeId);
        ms.write(oldOs, out.ipAddress);
        ms.write(oldOs, out.sources);
        ms.write(oldOs, out.dataBusErrors);
        ms.write(oldOs, out.sendTimeouts);
        ms.write(oldOs, out.deliveryFailures);
        ms.write(oldOs, out.capExceeded);
        ms.write(oldOs, out.pendingExceeded);
        ms.write(oldOs, out.lastEvent);
        dmq::ByteReader oldIs(oldOs);
        NodeHealthPacket old;
        old.poolPeak = 99;      // overwritten with 0
        old.read(ms, oldIs);
        DMQ_ASSERT_TRUE(!oldIs.fail() && oldIs.remaining() == 0);
        DMQ_ASSERT_TRUE(old.nodeId == "Pools" && old.lastEvent == "x");
        DMQ_ASSERT_TRUE(old.poolPeak == 0 && old.poolCapacity == 0 && old.poolPeakBytes == 0);
    }

#if defined(DMQ_ALLOCATOR)
    // 3c. Pool selection (detail::SelectPoolHealth(), what NodeReporter sends):
    //     the fullest static pool by peak/capacity, ties to fewer blocks left,
    //     heap-backed entries in the total only, the total clamped to 32 bits
    {
        using dmq::databus::detail::SelectPoolHealth;
        NodeHealthPacket none;
        SelectPoolHealth(nullptr, 0, none);
        DMQ_ASSERT_TRUE(none.sources == 0 && none.poolPeakBytes == 0);

        const XallocPoolStats mixed[] = {
            {16,  1,  3, 0},      // heap-backed: total only
            {32,  2, 10, 20},     // 50%
            {64,  0,  7, 8},      // 87%, one block left: the fullest
            {128, 0,  1, 4},      // 25%
        };
        NodeHealthPacket h;
        SelectPoolHealth(mixed, 4, h);
        DMQ_ASSERT_TRUE((h.sources & NodeHealthPacket::SRC_ALLOCATOR) != 0);
        DMQ_ASSERT_TRUE(h.poolBlockSize == 64 && h.poolPeak == 7 && h.poolCapacity == 8);
        DMQ_ASSERT_TRUE(h.poolPeakBytes == 16u * 3 + 32u * 10 + 64u * 7 + 128u * 1);

        // Equally full (90%): the pool with fewer blocks left, whichever comes first
        const XallocPoolStats tieA[] = { {32, 0, 18, 20}, {64, 0, 9, 10} };
        const XallocPoolStats tieB[] = { {32, 0, 9, 10}, {64, 0, 18, 20} };
        NodeHealthPacket ta, tb;
        SelectPoolHealth(tieA, 2, ta);
        SelectPoolHealth(tieB, 2, tb);
        DMQ_ASSERT_TRUE(ta.poolBlockSize == 64 && ta.poolPeak == 9);
        DMQ_ASSERT_TRUE(tb.poolBlockSize == 32 && tb.poolPeak == 9);

        // A small pool with one block left (75%) beats a larger one at 80% with
        // two left: dmq-monitor warns about the first, not the second
        const XallocPoolStats small[] = { {32, 0, 3, 4}, {64, 0, 8, 10} };
        NodeHealthPacket hs;
        SelectPoolHealth(small, 2, hs);
        DMQ_ASSERT_TRUE(hs.poolBlockSize == 32 && hs.poolPeak == 3 && hs.poolCapacity == 4);

        // Heap-backed only: a total, no fullest pool
        const XallocPoolStats heap[] = { {16, 0, 4, 0}, {32, 1, 2, 0} };
        NodeHealthPacket hh;
        SelectPoolHealth(heap, 2, hh);
        DMQ_ASSERT_TRUE(hh.poolPeakBytes == 16u * 4 + 32u * 2 && hh.poolCapacity == 0 && hh.poolBlockSize == 0);

        // A total past 4 GB reads as UINT32_MAX; full 32-bit counters don't overflow
        const XallocPoolStats huge[] = { {0xFFFFFFFFu, 0, 0xFFFFFFFFu, 0xFFFFFFFFu}, {16, 0, 1, 0xFFFFFFFFu} };
        NodeHealthPacket hg;
        SelectPoolHealth(huge, 2, hg);
        DMQ_ASSERT_TRUE(hg.poolPeakBytes == 0xFFFFFFFFu);
        DMQ_ASSERT_TRUE(hg.poolBlockSize == 0xFFFFFFFFu && hg.poolPeak == 0xFFFFFFFFu);
    }
#endif

    // 4. A failed send is a drop: counted and reported
    {
        DataBus::ResetForTesting();
        ReporterCaptureTransport transport;
        transport.Fail(true);
        NodeReporter reporter;
        std::atomic<uint32_t> lastReported{0};
        auto conn = reporter.OnDropped.Connect(dmq::MakeDelegate([&](uint32_t total) { lastReported = total; }));
        DMQ_ASSERT_TRUE(reporter.Start(transport, "NodeD", "10.0.0.4", "", HEARTBEAT, std::chrono::seconds(5)));

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (reporter.GetDropped() < 2 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        DMQ_ASSERT_TRUE(reporter.GetDropped() >= 2);    // a heartbeat's info and health
        DMQ_ASSERT_TRUE(reporter.GetSent() == 0);
        reporter.Stop();
        DMQ_ASSERT_TRUE(lastReported.load() == reporter.GetDropped());
    }

    std::cout << "NodeReporterTest PASSED!" << std::endl;
    return 0;
}

#else

int NodeReporterTestMain() {
    return 0;
}

#endif
