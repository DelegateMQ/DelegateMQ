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
        DMQ_ASSERT_TRUE(health.sources == NodeHealthPacket::SRC_DATABUS);   // nothing watched
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
        DataBus::Publish(NodeReporter::THREAD_STATS_TOPIC, stats);

        dmq::util::ThreadStatsPacket received;
        DMQ_ASSERT_TRUE(transport.WaitFor(THREAD_STATS_REMOTE_ID,
            [](dmq::ByteReader& is, dmq::util::ThreadStatsPacket& p) {
                dmq::util::ThreadStatsPacketSerializer ser;
                ser.Read(is, p);
            },
            [](const dmq::util::ThreadStatsPacket& p) { return p.thread_name == "Pump"; }, received));
        DMQ_ASSERT_TRUE(received.queue_depth == 3);
        reporter.Stop();
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
