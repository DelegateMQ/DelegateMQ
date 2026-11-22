#include "DelegateMQ.h"
#if defined(DMQ_DATABUS)
#include "extras/databus/SpyReporter.h"
#endif
#include <condition_variable>
#include <mutex>
#include <vector>
#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

#if defined(DMQ_DATABUS)

using namespace dmq;
using namespace dmq::os;
using namespace dmq::databus;

namespace {

/// Records what SpyReporter sends. Hold(true) blocks Send() (to fill the bridge's
/// queue); Fail(true) makes it return an error.
class SpyCaptureTransport : public dmq::transport::ITransport {
public:
    int Send(const dmq::ByteWriter& os, const dmq::transport::DmqHeader& header) override {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_entered = true;
        m_cv.notify_all();
        m_cv.wait(lock, [this] { return !m_hold; });
        m_headers.push_back(header);
        m_payloads.emplace_back(os.data(), os.size());
        m_cv.notify_all();
        return m_fail ? -1 : 0;
    }

    int Receive(dmq::ByteReader& is, dmq::transport::DmqHeader&) override {
        is.assign(nullptr, 0);
        return -1;
    }

    void Hold(bool hold) { std::lock_guard<std::mutex> lock(m_mutex); m_hold = hold; m_cv.notify_all(); }
    void Fail(bool fail) { std::lock_guard<std::mutex> lock(m_mutex); m_fail = fail; }

    size_t Count() { std::lock_guard<std::mutex> lock(m_mutex); return m_payloads.size(); }

    bool WaitForSends(size_t n) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::seconds(2), [&] { return m_payloads.size() >= n; });
    }

    bool WaitForEntered() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, std::chrono::seconds(2), [&] { return m_entered; });
    }

    /// The i-th message: its header and the SpyPacket decoded from its payload
    bool Get(size_t i, dmq::transport::DmqHeader& header, SpyPacket& packet) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (i >= m_payloads.size()) return false;
        header = m_headers[i];
        serialize ms;
        dmq::ByteReader is(m_payloads[i].data(), m_payloads[i].size());
        ms.read(is, packet);
        return is.good();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<dmq::transport::DmqHeader> m_headers;
    std::vector<std::string> m_payloads;
    bool m_hold = false;
    bool m_fail = false;
    bool m_entered = false;
};

} // namespace

int DataBusSpyTestMain() {
    std::cout << "Starting DataBusSpyTest..." << std::endl;

    DataBus::ResetForTesting();

    // Register stringifiers for topics we want to spy on
    DataBus::RegisterStringifier<int>("sensor/temp", dmq::MakeDelegate([](int val) -> dmq::xstring {
        char text[32];
        snprintf(text, sizeof(text), "%d C", val);
        return text;
    }));
    DataBus::RegisterStringifier<dmq::xstring>("system/status", dmq::MakeDelegate([](const dmq::xstring& val) -> dmq::xstring {
        return val;
    }));

    dmq::xstring lastTopic;
    dmq::xstring lastValue;
    uint64_t lastTimestamp = 0;

    // Connect the spy monitor
    auto spyConn = DataBus::Monitor(dmq::MakeDelegate([&](const SpyPacket& packet) {
        std::cout << "[SPY] " << packet.timestamp_us << " " << packet.topic.c_str() << " = " << packet.value.c_str() << std::endl;
        lastTopic = packet.topic;
        lastValue = packet.value;
        lastTimestamp = packet.timestamp_us;
    }));

    // Publish data
    DataBus::Publish<int>("sensor/temp", 22);
    DMQ_ASSERT_TRUE(lastTopic == "sensor/temp");
    DMQ_ASSERT_TRUE(lastValue == "22 C");
    DMQ_ASSERT_TRUE(lastTimestamp > 0);

    DataBus::Publish<dmq::xstring>("system/status", "OK");
    DMQ_ASSERT_TRUE(lastTopic == "system/status");
    DMQ_ASSERT_TRUE(lastValue == "OK");

    // Publish something without a stringifier
    DataBus::Publish<float>("unknown/topic", 1.23f);
    DMQ_ASSERT_TRUE(lastTopic == "unknown/topic");
    DMQ_ASSERT_TRUE(lastValue == "?");

    // 2. Async monitor — callback dispatches to the specified worker thread
    {
        DataBus::ResetForTesting();
        Thread monThread("MonitorWorker");
        monThread.CreateThread();

        std::atomic<bool> monitorFired{false};
        std::atomic<bool> calledOnWorker{false};

        auto monConn = DataBus::Monitor(dmq::MakeDelegate([&](const SpyPacket&) {
            calledOnWorker = monThread.IsCurrentThread();
            monitorFired = true;
        }), &monThread);

        DataBus::Publish<int>("async/monitor", 7);

        int retries = 0;
        while (!monitorFired && retries++ < 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        DMQ_ASSERT_TRUE(monitorFired == true);
        DMQ_ASSERT_TRUE(calledOnWorker == true);

        monThread.ExitThread();
    }

    // 3. SpyReporter: each DataBus message goes out through the transport as one
    //    message with remote ID SPY_REMOTE_ID, carrying a SpyPacket with this
    //    node's name.
    {
        DataBus::ResetForTesting();
        DataBus::RegisterStringifier<int>("spy/bridge", dmq::MakeDelegate([](int val) -> dmq::xstring {
            char text[16];
            snprintf(text, sizeof(text), "%d", val);
            return text;
        }));

        SpyCaptureTransport transport;
        SpyReporter bridge;
        DMQ_ASSERT_TRUE(bridge.Start(transport, "NodeA"));

        DataBus::Publish<int>("spy/bridge", 42);
        DMQ_ASSERT_TRUE(transport.WaitForSends(1));

        dmq::transport::DmqHeader header;
        SpyPacket packet;
        DMQ_ASSERT_TRUE(transport.Get(0, header, packet));
        DMQ_ASSERT_TRUE(header.GetId() == SPY_REMOTE_ID);
        DMQ_ASSERT_TRUE(packet.topic == "spy/bridge");
        DMQ_ASSERT_TRUE(packet.value == "42");
        DMQ_ASSERT_TRUE(packet.nodeId == "NodeA");
        DMQ_ASSERT_TRUE(packet.timestamp_us > 0);
        DMQ_ASSERT_TRUE(bridge.GetSent() == 1);
        DMQ_ASSERT_TRUE(bridge.GetDropped() == 0);

        // Stopped: nothing more goes out
        bridge.Stop();
        DataBus::Publish<int>("spy/bridge", 43);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        DMQ_ASSERT_TRUE(transport.Count() == 1);
    }

    // 4. SpyReporter never stalls the publisher: while the transport is stuck,
    //    messages beyond the queue are dropped, counted and reported.
    {
        DataBus::ResetForTesting();

        SpyCaptureTransport transport;
        transport.Hold(true);
        SpyReporter bridge(2);
        std::atomic<uint32_t> lastReported{0};
        auto dropConn = bridge.OnDropped.Connect(dmq::MakeDelegate([&](uint32_t total) {
            lastReported = total;
        }));
        DMQ_ASSERT_TRUE(bridge.Start(transport, "NodeB"));

        // The first message blocks the bridge's thread inside Send()
        DataBus::Publish<int>("spy/drop", 0);
        DMQ_ASSERT_TRUE(transport.WaitForEntered());

        constexpr int MORE = 10;
        for (int i = 1; i <= MORE; ++i)
            DataBus::Publish<int>("spy/drop", i);   // returns at once: never waits for the bridge

        // A queue of 2 can't hold 10
        DMQ_ASSERT_TRUE(bridge.GetDropped() > 0);
        DMQ_ASSERT_TRUE(lastReported.load() == bridge.GetDropped());

        transport.Hold(false);
        const uint32_t total = 1 + MORE;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (bridge.GetSent() + bridge.GetDropped() < total && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        DMQ_ASSERT_TRUE(bridge.GetSent() + bridge.GetDropped() == total);
        bridge.Stop();
    }

    // 5. A failed send is a drop too
    {
        DataBus::ResetForTesting();

        SpyCaptureTransport transport;
        transport.Fail(true);
        SpyReporter bridge;
        DMQ_ASSERT_TRUE(bridge.Start(transport, "NodeC", std::chrono::seconds(5)));   // with a watchdog
        DataBus::Publish<int>("spy/fail", 1);
        DMQ_ASSERT_TRUE(transport.WaitForSends(1));
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (bridge.GetDropped() < 1 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        DMQ_ASSERT_TRUE(bridge.GetDropped() == 1);
        DMQ_ASSERT_TRUE(bridge.GetSent() == 0);
        bridge.Stop();
    }

    std::cout << "DataBusSpyTest PASSED!" << std::endl;
    return 0;
}

#else

int DataBusSpyTestMain() {
    return 0;
}

#endif
