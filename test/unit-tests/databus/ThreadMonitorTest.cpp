/// @file ThreadMonitorTest.cpp
/// @brief Tests of the thread statistics behind ThreadMonitor: a thread's own
/// counters (Thread::SnapshotStats), the ThreadStatsPacket serializer, and
/// ThreadMonitor publishing registered threads' stats on the DataBus.
///
/// Timing checks use lower bounds only (a message held back 100 ms must report
/// at least 50 ms), so a slow machine or a sanitizer build can't fail them.

#include "DelegateMQ.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(DMQ_DATABUS) && defined(DMQ_DATABUS_TOOLS) && defined(DMQ_THREAD_STDLIB)

#include "extras/util/ThreadMonitor.h"
#include "extras/util/ThreadMonitorSer.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace dmq::databus;

namespace {

constexpr auto HOLD = std::chrono::milliseconds(100);
constexpr float HOLD_MIN_MS = 50.0f;
constexpr float ROUNDING_MS = 0.001f;   // stats are whole microseconds, see SnapshotStatsTest()

/// Wait until pred() is true or timeout passes; return pred().
template <class Pred>
bool WaitFor(Pred pred, std::chrono::milliseconds timeout)
{
    auto end = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return pred();
}

/// A thread's own counters: queue depth, wait time in the queue (latency),
/// time spent in the call (invoke), dispatch count, and the per-window reset
/// each snapshot does.
void SnapshotStatsTest()
{
    const size_t QUEUED = 5;
    Thread worker("StatsWorker", 100, FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "StatsCPU");
    DMQ_ASSERT_TRUE(worker.CreateThread());

    auto s0 = worker.SnapshotStats();
    DMQ_ASSERT_TRUE(s0.thread_name == "StatsWorker");
    DMQ_ASSERT_TRUE(s0.cpu_name == "StatsCPU");
    DMQ_ASSERT_TRUE(s0.queue_size_limit == 100);
    DMQ_ASSERT_TRUE(s0.queue_depth == 0);
    DMQ_ASSERT_TRUE(s0.dispatch_count == 0);
    // The desktop ports don't track the stack (the RTOS ports' readings are
    // checked by test/portable/StackPortableTest.h on each OS sample)
    DMQ_ASSERT_TRUE(s0.stack_size == dmq::STACK_UNKNOWN);
    DMQ_ASSERT_TRUE(s0.stack_free_min == dmq::STACK_UNKNOWN);

    // A blocker holds the worker; QUEUED quick messages pile up behind it.
    std::atomic<bool> started{false}, release{false};
    std::atomic<int> done{0};
    MakeDelegate(std::function<void()>([&]() {
        started = true;
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }), worker).AsyncInvoke();
    DMQ_ASSERT_TRUE(WaitFor([&] { return started.load(); }, std::chrono::seconds(5)));

    for (size_t i = 0; i < QUEUED; ++i)
        MakeDelegate(std::function<void()>([&]() { done++; }), worker).AsyncInvoke();

    // Queued behind the blocker: depth now QUEUED, the most it reached.
    auto s1 = worker.SnapshotStats();
    DMQ_ASSERT_TRUE(s1.queue_depth == QUEUED);
    DMQ_ASSERT_TRUE(s1.queue_depth_max_window == QUEUED);
    DMQ_ASSERT_TRUE(s1.queue_depth_max_all == QUEUED);
    DMQ_ASSERT_TRUE(s1.dispatch_count == 1);   // the blocker, taken off the queue

    std::this_thread::sleep_for(HOLD);
    release = true;
    DMQ_ASSERT_TRUE(WaitFor([&] { return done.load() == static_cast<int>(QUEUED); }, std::chrono::seconds(5)));
    DMQ_ASSERT_TRUE(WaitFor([&] { return worker.GetQueueSize() == 0; }, std::chrono::seconds(5)));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));   // last invoke's stats recorded

    // This window: the queued messages waited at least HOLD, the blocker ran at least HOLD.
    auto s2 = worker.SnapshotStats();
    DMQ_ASSERT_TRUE(s2.queue_depth == 0);
    DMQ_ASSERT_TRUE(s2.queue_depth_max_window == 0);   // no message queued since s1
    DMQ_ASSERT_TRUE(s2.queue_depth_max_all == QUEUED);
    DMQ_ASSERT_TRUE(s2.dispatch_count == 1 + QUEUED);
    DMQ_ASSERT_TRUE(s2.latency_max_window_ms >= HOLD_MIN_MS);
    DMQ_ASSERT_TRUE(s2.latency_max_all_ms >= s2.latency_max_window_ms);
    // The average can exceed the maximum by under 1 us: SnapshotStats() rounds the
    // window's total down to whole microseconds, and the maximum separately, and
    // the queued messages all waited nearly the same time.
    DMQ_ASSERT_TRUE(s2.latency_avg_ms > 0.0f && s2.latency_avg_ms <= s2.latency_max_window_ms + ROUNDING_MS);
    DMQ_ASSERT_TRUE(s2.invoke_max_window_ms >= HOLD_MIN_MS);
    DMQ_ASSERT_TRUE(s2.invoke_max_all_ms >= s2.invoke_max_window_ms);
    DMQ_ASSERT_TRUE(s2.invoke_avg_ms > 0.0f && s2.invoke_avg_ms <= s2.invoke_max_window_ms + ROUNDING_MS);

    // Nothing ran since s2: the window values are back to zero, all-time kept.
    auto s3 = worker.SnapshotStats();
    DMQ_ASSERT_TRUE(s3.latency_avg_ms == 0.0f);
    DMQ_ASSERT_TRUE(s3.latency_max_window_ms == 0.0f);
    DMQ_ASSERT_TRUE(s3.invoke_avg_ms == 0.0f);
    DMQ_ASSERT_TRUE(s3.invoke_max_window_ms == 0.0f);
    DMQ_ASSERT_TRUE(s3.latency_max_all_ms == s2.latency_max_all_ms);
    DMQ_ASSERT_TRUE(s3.invoke_max_all_ms == s2.invoke_max_all_ms);
    DMQ_ASSERT_TRUE(s3.queue_depth_max_all == QUEUED);
    DMQ_ASSERT_TRUE(s3.dispatch_count == 1 + QUEUED);

    worker.ExitThread();
    std::cout << "SnapshotStatsTest() complete!" << std::endl;
}

/// ThreadStatsPacketSerializer: every field survives Write then Read.
void SerializerTest()
{
    ThreadStatsPacket in;
    in.cpu_name = "GUI";
    in.thread_name = "Alarms";
    in.queue_depth = 3;
    in.queue_depth_max_window = 7;
    in.queue_depth_max_all = 42;
    in.queue_size_limit = 1000;
    in.latency_avg_ms = 1.25f;
    in.latency_max_window_ms = 2.5f;
    in.latency_max_all_ms = 12.75f;
    in.invoke_avg_ms = 0.5f;
    in.invoke_max_window_ms = 3.0f;
    in.invoke_max_all_ms = 9.5f;
    in.dispatch_count = 0x1234567890ULL;   // needs more than 32 bits
    in.stack_size = 4096;
    in.stack_free_min = 612;

    ThreadStatsPacketSerializer ser;
    dmq::ByteWriter ss;
    ser.Write(ss, in);
    DMQ_ASSERT_TRUE(ss.good());

    ThreadStatsPacket out;
    dmq::ByteReader rs(ss);
    ser.Read(rs, out);
    DMQ_ASSERT_TRUE(!rs.fail());
    DMQ_ASSERT_TRUE(rs.remaining() == 0);
    DMQ_ASSERT_TRUE(out.cpu_name == in.cpu_name);
    DMQ_ASSERT_TRUE(out.thread_name == in.thread_name);
    DMQ_ASSERT_TRUE(out.queue_depth == in.queue_depth);
    DMQ_ASSERT_TRUE(out.queue_depth_max_window == in.queue_depth_max_window);
    DMQ_ASSERT_TRUE(out.queue_depth_max_all == in.queue_depth_max_all);
    DMQ_ASSERT_TRUE(out.queue_size_limit == in.queue_size_limit);
    DMQ_ASSERT_TRUE(out.latency_avg_ms == in.latency_avg_ms);
    DMQ_ASSERT_TRUE(out.latency_max_window_ms == in.latency_max_window_ms);
    DMQ_ASSERT_TRUE(out.latency_max_all_ms == in.latency_max_all_ms);
    DMQ_ASSERT_TRUE(out.invoke_avg_ms == in.invoke_avg_ms);
    DMQ_ASSERT_TRUE(out.invoke_max_window_ms == in.invoke_max_window_ms);
    DMQ_ASSERT_TRUE(out.invoke_max_all_ms == in.invoke_max_all_ms);
    DMQ_ASSERT_TRUE(out.dispatch_count == in.dispatch_count);
    DMQ_ASSERT_TRUE(out.stack_size == in.stack_size);
    DMQ_ASSERT_TRUE(out.stack_free_min == in.stack_free_min);

    // The text form the spy console shows names the thread and its CPU, and
    // the stack as least free / size
    dmq::xstring text = ThreadStatsPacketToString(in);
    DMQ_ASSERT_TRUE(text.find("CPU:GUI") != dmq::xstring::npos);
    DMQ_ASSERT_TRUE(text.find("Thread:Alarms") != dmq::xstring::npos);
    DMQ_ASSERT_TRUE(text.find("Stack free/size:612/4096") != dmq::xstring::npos);

    // A packet's stack fields default to unknown, shown as "n/a"
    ThreadStatsPacket unknown;
    DMQ_ASSERT_TRUE(unknown.stack_size == dmq::STACK_UNKNOWN);
    DMQ_ASSERT_TRUE(unknown.stack_free_min == dmq::STACK_UNKNOWN);
    DMQ_ASSERT_TRUE(ThreadStatsPacketToString(unknown).find("Stack free/size:n/a/n/a") != dmq::xstring::npos);

    std::cout << "SerializerTest() complete!" << std::endl;
}

/// ThreadMonitor publishes a registered thread's stats on its topic as soon as
/// it is enabled, and stops including the thread once it is deregistered.
void MonitorPublishTest()
{
    const char* TOPIC = "test/ThreadStats";
    DataBus::ResetForTesting();

    Thread worker("MonWorker", 50, FullPolicy::FAULT, dmq::DEFAULT_DISPATCH_TIMEOUT, "MonCPU");
    DMQ_ASSERT_TRUE(worker.CreateThread());

    // Give it some traffic so the packet has something to report
    std::atomic<int> ran{0};
    for (int i = 0; i < 3; ++i)
        MakeDelegate(std::function<void()>([&]() { ran++; }), worker).AsyncInvoke();
    DMQ_ASSERT_TRUE(WaitFor([&] { return ran.load() == 3; }, std::chrono::seconds(5)));

    std::mutex m;
    ThreadStatsPacket last;
    std::atomic<int> received{0};
    auto conn = DataBus::Subscribe<ThreadStatsPacket>(TOPIC, [&](const ThreadStatsPacket& p) {
        if (p.thread_name != "MonWorker") return;   // other registered threads, if any
        std::lock_guard<std::mutex> lock(m);
        last = p;
        received++;
    });

    ThreadMonitor::Register(&worker);
    ThreadMonitor::Register(&worker);   // a second Register is ignored
    ThreadMonitor::Enable(TOPIC);       // the first poll runs at once
    DMQ_ASSERT_TRUE(WaitFor([&] { return received.load() >= 1; }, std::chrono::seconds(5)));
    {
        std::lock_guard<std::mutex> lock(m);
        DMQ_ASSERT_TRUE(last.cpu_name == "MonCPU");
        DMQ_ASSERT_TRUE(last.queue_size_limit == 50);
        DMQ_ASSERT_TRUE(last.dispatch_count == 3);
        DMQ_ASSERT_TRUE(last.queue_depth == 0);
    }
    // One packet per registered thread per poll, not two
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    DMQ_ASSERT_TRUE(received.load() == 1);
    ThreadMonitor::Disable();

    // Deregistered: a new poll leaves it out
    ThreadMonitor::Deregister(&worker);
    received = 0;
    ThreadMonitor::Enable(TOPIC);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    DMQ_ASSERT_TRUE(received.load() == 0);
    ThreadMonitor::Disable();

    worker.ExitThread();
    DataBus::ResetForTesting();
    std::cout << "MonitorPublishTest() complete!" << std::endl;
}

} // namespace

int ThreadMonitorTestMain()
{
    std::cout << "Starting ThreadMonitorTest..." << std::endl;
    SnapshotStatsTest();
    SerializerTest();
    MonitorPublishTest();
    std::cout << "ThreadMonitorTest PASSED!" << std::endl;
    return 0;
}

#else

int ThreadMonitorTestMain()
{
    return 0;
}

#endif
