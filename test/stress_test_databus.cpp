/// @file stress_test_databus.cpp
/// @brief Long-term multi-threaded stress test for DelegateMQ DataBus features.
///
/// @details Tests the following DataBus features under concurrent high-volume load:
///
///   LOCAL PUB/SUB — Integrity verified:
///     • 4 publisher threads × 2 topics (sync + async)
///     • 2 synchronous subscribers on "db/sync"  → g_db_syncReceived == g_db_expectedSync
///     • 3 async subscriber threads on "db/async" → g_db_asyncReceived == g_db_expectedAsync
///
///   QoS minSeparation — Rate-limiting verified:
///     • throttled subscriber (20 ms min gap) on "db/rate": throttled <= unthrottled
///
///   QoS SubscribeFilter — Predicate gating verified:
///     • even-only filter on "db/filter": g_db_filterPassed == g_db_filterExpected
///
///   QoS Last Value Cache — Spot-checked throughout run:
///     • LVC enabled on "db/async"
///     • Checker thread subscribes every 50 ms: g_db_lvcHits == g_db_lvcChecks
///
///   REMOTE PATH — integrity tracked:
///     • Publisher → "db/remote" → remoteParticipant.Send → DbLoopbackTransport queue
///     • Receive thread → echoParticipant.ProcessIncoming() → PublishLocal("db/echo")
///     • Echo subscriber increments g_db_remoteReceived (compared to g_db_remoteSent)
///
///   MONITOR — coverage verified:
///     • Synchronous monitor callback: g_db_monitorCount > 0
///
///   UNHANDLED — integrity verified:
///     • "db/ghost" has no subscribers → SubscribeUnhandled fires every publish
///     • g_db_unhandledCount == g_db_ghostPub
///
///   CHAOS MONKEY — thread-safety only:
///     • Rapidly connects/disconnects a volatile async subscriber on "db/async"
///     • Counts NOT included in integrity totals
///
/// @section Usage
///   Uncomment #define STRESS_TEST_DATABUS in test/main.cpp, then build Release.

#include "DelegateMQ.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <queue>
#include <mutex>
#include <condition_variable>

#if defined(DMQ_DATABUS)

using namespace dmq;
using namespace dmq::os;
using namespace dmq::transport;
using namespace dmq::databus;
using namespace dmq::serialization::serializer;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static constexpr int     DB_TEST_DURATION_SEC = 20;
static constexpr int     DB_BURST_SIZE        = 500;
static constexpr int     DB_MAX_QUEUE         = 200;
static constexpr int     DB_NUM_PUB_THREADS   = 4;   // publish to sync + async topics
static constexpr int     DB_NUM_SYNC_SUBS     = 2;
static constexpr int     DB_NUM_ASYNC_SUBS    = 3;
static constexpr DelegateRemoteId DB_ECHO_REMOTE_ID = 77;

// ---------------------------------------------------------------------------
// Topics
// ---------------------------------------------------------------------------
static const dmq::xstring DB_TOPIC_SYNC   = "db/sync";
static const dmq::xstring DB_TOPIC_ASYNC  = "db/async";
static const dmq::xstring DB_TOPIC_RATE   = "db/rate";
static const dmq::xstring DB_TOPIC_FILTER = "db/filter";
static const dmq::xstring DB_TOPIC_REMOTE = "db/remote";
static const dmq::xstring DB_TOPIC_ECHO   = "db/echo";
static const dmq::xstring DB_TOPIC_GHOST  = "db/ghost";

// ---------------------------------------------------------------------------
// Global counters
// ---------------------------------------------------------------------------
static std::atomic<uint64_t> g_db_expectedSync{0};
static std::atomic<uint64_t> g_db_syncReceived{0};
static std::atomic<uint64_t> g_db_expectedAsync{0};
static std::atomic<uint64_t> g_db_asyncReceived{0};

static std::atomic<uint64_t> g_db_rateThrottled{0};
static std::atomic<uint64_t> g_db_rateUnthrottled{0};

static std::atomic<uint64_t> g_db_filterExpected{0}; // even publishes to db/filter
static std::atomic<uint64_t> g_db_filterPassed{0};

static std::atomic<uint64_t> g_db_remoteSent{0};
static std::atomic<uint64_t> g_db_remoteReceived{0};

static std::atomic<uint64_t> g_db_monitorCount{0};

static std::atomic<uint64_t> g_db_ghostPub{0};
static std::atomic<uint64_t> g_db_unhandledCount{0};

static std::atomic<uint64_t> g_db_lvcChecks{0};
static std::atomic<uint64_t> g_db_lvcHits{0};

static std::atomic<bool>     g_db_running{true};
static std::atomic<bool>     g_db_stopRemote{false};

// ---------------------------------------------------------------------------
// DbLoopbackTransport — thread-safe in-memory transport
//   Send() serializes the packet into a blocking queue.
//   Receive() blocks until a packet is available or Stop() is called.
// ---------------------------------------------------------------------------
class DbLoopbackTransport : public ITransport {
public:
    struct Packet {
        DmqHeader         header;
        std::vector<char> data;
    };

    int Send(xostringstream& os, const DmqHeader& header) override {
        xstring s = os.str();
        std::vector<char> d(s.begin(), s.end());
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push({header, std::move(d)});
        }
        m_cv.notify_one();
        return 0;
    }

    int Receive(xstringstream& is, DmqHeader& header) override {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return !m_queue.empty() || m_stopped; });
        if (m_queue.empty()) return -1; // stopped with queue drained
        auto p = std::move(m_queue.front());
        m_queue.pop();
        header = p.header;
        is.write(p.data.data(), p.data.size());
        return 0;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopped = true;
        }
        m_cv.notify_all();
    }

private:
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    std::queue<Packet>      m_queue;
    bool                    m_stopped = false;
};

static DbLoopbackTransport g_dbTransport;

// ---------------------------------------------------------------------------
// stress_test_databus
// ---------------------------------------------------------------------------
int stress_test_databus()
{
    std::cout << "==========================================\n";
    std::cout << "   DelegateMQ DataBus Stress Test         \n";
    std::cout << "==========================================\n";
    std::cout << "Duration:   " << DB_TEST_DURATION_SEC << "s\n";
    std::cout << "Publishers: " << DB_NUM_PUB_THREADS << " (db/sync + db/async)\n";
    std::cout << "Sync subs:  " << DB_NUM_SYNC_SUBS << "\n";
    std::cout << "Async subs: " << DB_NUM_ASYNC_SUBS << "\n";
    std::cout << "Features:   LVC, minSep, Filter, Remote, Monitor, Unhandled, Chaos\n";
    std::cout << "Starting...\n" << std::endl;

    DataBus::ResetForTesting();

    // -----------------------------------------------------------------------
    // Monitor — synchronous (fires on publisher thread → exact count)
    // -----------------------------------------------------------------------
    auto monitorConn = DataBus::Monitor([](const SpyPacket&) {
        g_db_monitorCount++;
    });

    // -----------------------------------------------------------------------
    // SubscribeUnhandled — "db/ghost" has no data subscribers → fires here
    // -----------------------------------------------------------------------
    auto unhandledConn = DataBus::SubscribeUnhandled([](const dmq::xstring&) {
        g_db_unhandledCount++;
    });

    // -----------------------------------------------------------------------
    // Enable LVC on "db/async" so late subscribers receive cached value
    // -----------------------------------------------------------------------
    DataBus::LastValueCache(DB_TOPIC_ASYNC, true);

    // -----------------------------------------------------------------------
    // Sync subscribers — called on publisher thread, count is exact
    // -----------------------------------------------------------------------
    std::vector<ScopedConnection> syncConns;
    for (int i = 0; i < DB_NUM_SYNC_SUBS; ++i) {
        syncConns.push_back(DataBus::Subscribe<int>(DB_TOPIC_SYNC, [](int) {
            g_db_syncReceived++;
        }));
    }

    // -----------------------------------------------------------------------
    // Async subscribers — each dispatched to its own worker thread
    // -----------------------------------------------------------------------
    std::vector<std::unique_ptr<Thread>> asyncSubThreads;
    std::vector<ScopedConnection>        asyncConns;
    for (int i = 0; i < DB_NUM_ASYNC_SUBS; ++i) {
        auto t = std::make_unique<Thread>(
            "DbAsyncSub_" + std::to_string(i), DB_MAX_QUEUE, FullPolicy::TIMEOUT);
        t->CreateThread();
        asyncConns.push_back(DataBus::Subscribe<int>(DB_TOPIC_ASYNC, [](int) {
            g_db_asyncReceived++;
        }, t.get()));
        asyncSubThreads.push_back(std::move(t));
    }

    // -----------------------------------------------------------------------
    // QoS minSeparation — pair on "db/rate"
    //   throttled:   max 1 delivery per 20 ms regardless of publish rate
    //   unthrottled: receives every publish
    //   Invariant: throttled <= unthrottled, both > 0
    // -----------------------------------------------------------------------
    QoS throttleQos;
    throttleQos.minSeparation = std::chrono::milliseconds(20);
    auto rateThrottledConn = DataBus::Subscribe<int>(DB_TOPIC_RATE, [](int) {
        g_db_rateThrottled++;
    }, nullptr, throttleQos);

    auto rateUnthrottledConn = DataBus::Subscribe<int>(DB_TOPIC_RATE, [](int) {
        g_db_rateUnthrottled++;
    });

    // -----------------------------------------------------------------------
    // QoS SubscribeFilter — even values only on "db/filter"
    //   Invariant: g_db_filterPassed == g_db_filterExpected
    // -----------------------------------------------------------------------
    auto filterConn = DataBus::SubscribeFilter<int>(DB_TOPIC_FILTER,
        [](int) { g_db_filterPassed++; },
        [](int v) { return (v % 2) == 0; });

    // -----------------------------------------------------------------------
    // Remote path
    //   remoteParticipant: maps "db/remote" → ECHO_REMOTE_ID via g_dbTransport
    //   echoParticipant:   receives on ECHO_REMOTE_ID → PublishLocal("db/echo")
    //   echoConn:          counts deliveries for integrity check
    //   receiveThread:     drives echoParticipant.ProcessIncoming() in a blocking loop
    // -----------------------------------------------------------------------
    Serializer<void(int)> dbRemoteSerializer;

    auto remoteParticipant = dmq::xmake_shared<Participant>(g_dbTransport);
    remoteParticipant->AddRemoteTopic(DB_TOPIC_REMOTE, DB_ECHO_REMOTE_ID);
    DataBus::AddParticipant(remoteParticipant);
    DataBus::RegisterSerializer<int>(DB_TOPIC_REMOTE, dbRemoteSerializer);

    auto echoParticipant = dmq::xmake_shared<Participant>(g_dbTransport);
    DataBus::AddIncomingTopic<int>(DB_TOPIC_ECHO, DB_ECHO_REMOTE_ID, *echoParticipant, dbRemoteSerializer);

    auto echoConn = DataBus::Subscribe<int>(DB_TOPIC_ECHO, [](int) {
        g_db_remoteReceived++;
    });

    std::thread receiveThread([&echoParticipant]() {
        while (true) {
            int result = echoParticipant->ProcessIncoming();
            if (result == -1) break; // transport stopped and queue drained
        }
    });

    // -----------------------------------------------------------------------
    // Publisher threads — "db/sync" and "db/async" (integrity verified)
    //   Expectations are updated BEFORE publish to avoid the shutdown race
    //   where g_running becomes false between the increment and the emit.
    // -----------------------------------------------------------------------
    std::vector<std::thread> pubThreads;
    for (int i = 0; i < DB_NUM_PUB_THREADS; ++i) {
        pubThreads.emplace_back([i]() {
            int seq = i * 10000000;
            while (g_db_running) {
                for (int b = 0; b < DB_BURST_SIZE && g_db_running; ++b) {
                    g_db_expectedSync  += DB_NUM_SYNC_SUBS;
                    g_db_expectedAsync += DB_NUM_ASYNC_SUBS;
                    DataBus::Publish<int>(DB_TOPIC_SYNC,  seq);
                    DataBus::Publish<int>(DB_TOPIC_ASYNC, seq);
                    ++seq;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // Rate-limiting + filter publisher — one thread, sequential seq counter
    //   g_db_filterExpected tracks even-valued publishes to db/filter;
    //   the filter lambda fires synchronously so counts stay in lock-step.
    std::thread ratePub([]() {
        int seq = 0;
        while (g_db_running) {
            for (int b = 0; b < DB_BURST_SIZE && g_db_running; ++b) {
                DataBus::Publish<int>(DB_TOPIC_RATE, seq);
                if ((seq % 2) == 0) g_db_filterExpected++;
                DataBus::Publish<int>(DB_TOPIC_FILTER, seq);
                ++seq;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Remote publisher — sends through the full remote path
    std::thread remotePub([]() {
        int seq = 0;
        while (g_db_running) {
            for (int b = 0; b < DB_BURST_SIZE && g_db_running; ++b) {
                g_db_remoteSent++;
                DataBus::Publish<int>(DB_TOPIC_REMOTE, seq++);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Ghost publisher — "db/ghost" has no subscribers; every publish fires SubscribeUnhandled
    std::thread ghostPub([]() {
        int seq = 0;
        while (g_db_running) {
            for (int b = 0; b < 20 && g_db_running; ++b) {
                g_db_ghostPub++;
                DataBus::Publish<int>(DB_TOPIC_GHOST, seq++);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // -----------------------------------------------------------------------
    // Chaos Monkey — rapidly connects/disconnects a volatile async subscriber
    //   on "db/async" to stress container thread-safety. Counts NOT tracked
    //   in integrity totals.
    // -----------------------------------------------------------------------
    Thread chaosWorker("DbChaosWorker");
    chaosWorker.CreateThread();

    std::thread chaosControl([&chaosWorker]() {
        while (g_db_running) {
            auto conn = DataBus::Subscribe<int>(DB_TOPIC_ASYNC,
                [](int) { /* volatile slot — not counted */ }, &chaosWorker);
            std::this_thread::sleep_for(std::chrono::microseconds(300));
            conn.Disconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // -----------------------------------------------------------------------
    // LVC Checker — subscribes with lastValueCache every 50 ms and verifies
    //   that the cached value is delivered synchronously.
    //   With no thread arg, DataBus::Subscribe delivers LVC inline before
    //   returning, so `received` reflects the LVC state before conn is dropped.
    // -----------------------------------------------------------------------
    std::thread lvcChecker([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // warmup
        while (g_db_running) {
            bool received = false;
            {
                QoS qos;
                qos.lastValueCache = true;
                auto conn = DataBus::Subscribe<int>(DB_TOPIC_ASYNC,
                    [&](int) { received = true; },
                    nullptr, qos); // no thread → LVC fires synchronously inside Subscribe
            } // conn auto-disconnects here
            g_db_lvcChecks++;
            if (received) g_db_lvcHits++;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    // -----------------------------------------------------------------------
    // Progress monitor loop
    // -----------------------------------------------------------------------
    auto testStart = std::chrono::steady_clock::now();
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - testStart).count();

        uint64_t local = g_db_syncReceived + g_db_asyncReceived;
        std::cout << "[" << std::setw(3) << elapsed << "s] "
            << "Sync: "    << g_db_syncReceived  << " | "
            << "Async: "   << g_db_asyncReceived << " | "
            << "Rate: "    << (local / (elapsed > 0 ? elapsed : 1)) << " msg/s | "
            << "Remote: "  << g_db_remoteReceived << "/" << g_db_remoteSent << " | "
            << "Monitor: " << g_db_monitorCount  << " | "
            << "LVC: "     << g_db_lvcHits << "/" << g_db_lvcChecks
            << "\n";

        if (elapsed >= DB_TEST_DURATION_SEC) break;
    }

    // -----------------------------------------------------------------------
    // Shutdown — ordered to prevent in-flight message loss
    //   1. Stop producers first so g_db_expectedSync/Async are final.
    //   2. Drain async subscriber queues (3 s) before checking counts.
    //   3. Stop the blocking transport so receiveThread can exit cleanly.
    //   4. Exit worker threads.
    // -----------------------------------------------------------------------
    std::cout << "\nStopping publishers...\n";
    g_db_running = false;

    for (auto& t : pubThreads) if (t.joinable()) t.join();
    if (ratePub.joinable())      ratePub.join();
    if (remotePub.joinable())    remotePub.join();
    if (ghostPub.joinable())     ghostPub.join();
    if (chaosControl.joinable()) chaosControl.join();
    if (lvcChecker.joinable())   lvcChecker.join();

    std::cout << "Draining async queues and remote transport (3s)...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));

    g_db_stopRemote = true;
    g_dbTransport.Stop();
    if (receiveThread.joinable()) receiveThread.join();

    chaosWorker.ExitThread();
    for (auto& t : asyncSubThreads) t->ExitThread();

    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // final drain

    syncConns.clear();
    asyncConns.clear();

    // -----------------------------------------------------------------------
    // Final Integrity Report
    // -----------------------------------------------------------------------
    const uint64_t expSync      = g_db_expectedSync.load();
    const uint64_t actSync      = g_db_syncReceived.load();
    const uint64_t expAsync     = g_db_expectedAsync.load();
    const uint64_t actAsync     = g_db_asyncReceived.load();
    const uint64_t rSent        = g_db_remoteSent.load();
    const uint64_t rRecv        = g_db_remoteReceived.load();
    const uint64_t throttled    = g_db_rateThrottled.load();
    const uint64_t unthrottled  = g_db_rateUnthrottled.load();
    const uint64_t filtPassed   = g_db_filterPassed.load();
    const uint64_t filtExpected = g_db_filterExpected.load();
    const uint64_t lvcHits      = g_db_lvcHits.load();
    const uint64_t lvcChecks    = g_db_lvcChecks.load();
    const uint64_t ghost        = g_db_ghostPub.load();
    const uint64_t unhandled    = g_db_unhandledCount.load();
    const uint64_t monitor      = g_db_monitorCount.load();

    bool pass = true;

    auto report = [&](const char* label, uint64_t actual, uint64_t expected, bool warnOnly = false) {
        bool ok = (actual == expected) && (expected > 0);
        std::cout << std::left << std::setw(22) << label
                  << " actual=" << std::setw(14) << actual
                  << " expected=" << expected;
        if (ok)             { std::cout << " [OK]\n"; }
        else if (warnOnly)  { std::cout << " [WARN — async drain]\n"; }
        else                { std::cout << " [FAIL]\n"; pass = false; }
    };

    std::cout << "==========================================\n";
    std::cout << "Final DataBus Stress Test Report:\n";
    std::cout << "------------------------------------------\n";

    report("Sync Recv",     actSync,    expSync);
    report("Async Recv",    actAsync,   expAsync);
    report("Remote Recv",   rRecv,      rSent,       /*warnOnly=*/true);
    report("Filter Passed", filtPassed, filtExpected);
    report("LVC Hits",      lvcHits,    lvcChecks);
    report("Unhandled",     unhandled,  ghost);

    // Rate: throttled must be strictly less than unthrottled and both must exist
    bool rateOk = (throttled < unthrottled) && (throttled > 0);
    std::cout << std::left << std::setw(22) << "Rate Throttled"
              << " throttled=" << std::setw(10) << throttled
              << " unthrottled=" << unthrottled
              << (rateOk ? " [OK]\n" : " [FAIL]\n");
    if (!rateOk) pass = false;

    bool monOk = (monitor > 0);
    std::cout << std::left << std::setw(22) << "Monitor Events"
              << " count=" << monitor
              << (monOk ? " [OK]\n" : " [FAIL]\n");
    if (!monOk) pass = false;

    std::cout << "------------------------------------------\n";
    if (pass) std::cout << "[SUCCESS] ALL DATABUS CHECKS PASSED.\n";
    else      std::cout << "[FAILURE] ONE OR MORE DATABUS CHECKS FAILED.\n";
    std::cout << "==========================================\n";

    return pass ? 0 : 1;
}

#else

int stress_test_databus()
{
    std::cout << "stress_test_databus: DMQ_DATABUS not enabled — skipping.\n";
    return 0;
}

#endif
