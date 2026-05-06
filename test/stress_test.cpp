/// @file stress_test.cpp
/// @brief Long-term multi-threaded stress test for DelegateMQ with Integrity Verification.
///
/// @details This test creates a high-contention environment to benchmark stability, throughput,
/// and correctness. It performs the following:
/// 1. Multiple Publisher threads firing Sync and Async signals to saturate the bus.
/// 2. Multiple Subscriber threads processing events with artificial workload.
/// 3. A "Chaos Thread" that randomly connects/disconnects a volatile slot to test container thread-safety.
/// 4. STRICT INTEGRITY CHECK: Verifies that (Signals Fired * Subscriber Count) matches exactly
///    with the number of messages received by the stable subscribers.
/// 
/// @section Allocator Usage
/// This test utilizes the DelegateMQ Fixed Block Allocator (optional) to maximize throughput on systems
/// with slow heap allocators (e.g., embedded).
/// - The macro 'DMQ_ALLOCATOR' is defined before including DelegateMQ.h to enable the feature.
/// - The macro 'XALLOCATOR' is placed inside classes (Payload, StressPublisher, etc.) to 
///   overload 'operator new' and 'operator delete'.
///
/// @section Allocator Comparison (Windows Release Mode)
/// Results from testing on a modern PC (Windows 10/11) with Visual Studio 2026:
///
/// | Metric                 | Standard Heap (LFH)    | Fixed Block (XALLOCATOR) |
/// | :--------------------- | :--------------------- | :----------------------- |
/// | Throughput             | ~1,040,000 msg/s       | ~600,000 msg/s           |
/// | Thread Scaling         | Excellent (Lock-Free)  | Moderate (Mutex Contention)|
/// | Best For               | PC / Server Apps       | Embedded / RTOS / Determinism |
///
/// @note On Windows 10+, the OS "Low Fragmentation Heap" is highly optimized for 
/// concurrency and outperforms the simple mutex-based XALLOCATOR. XALLOCATOR remains 
/// superior for embedded systems where the system heap is slow, non-deterministic,
/// or prone to fragmentation over long uptimes.

#include "DelegateMQ.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>

// --- Memory Leak Detection (Windows) ---
// Uncomment the line below to enable leak checking on Windows Debug builds
//#define CHECK_LEAKS 

#if defined(CHECK_LEAKS) && defined(_MSC_VER) && defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace dmq::transport;

// --- Configuration ---
const int NUM_PUBLISHERS = 4;
const int NUM_SUBSCRIBERS = 4; // Must be even for fair sync/async split
const int TEST_DURATION_SECONDS = 30;
const int BURST_SIZE = 1000;
const int MAX_QUEUE_SIZE = 100; // Constant for thread max queue "Back Pressure"

// --- Global Integrity Counters ---
// We track exactly how many messages *should* be received based on fire count.
static std::atomic<uint32_t> g_signalsFired{ 0 };
static std::atomic<uint32_t> g_syncReceived{ 0 };
static std::atomic<uint32_t> g_asyncReceived{ 0 };

// Expected counts (calculated at emission time)
static std::atomic<uint32_t> g_expectedSync{ 0 };
static std::atomic<uint32_t> g_expectedAsync{ 0 };

static std::atomic<bool>     g_running{ true };

// --- Payload ---
struct Payload
{
    XALLOCATOR

    int id;
    int publisherId;
    char buffer[64];

    Payload(int i, int p) : id(i), publisherId(p) {
        snprintf(buffer, sizeof(buffer), "Pub-%d Msg-%d", p, i);
    }
};

// --- Publisher ---
class StressPublisher
{
public:
    XALLOCATOR

    using StressSig = void(const Payload&);

    dmq::Signal<StressSig> OnMessage;

    void Start(int id)
    {
        m_thread = std::thread([this, id]() {
            int msgId = 0;

            // Calculate fan-out once (assumes static topology for verification)
            // Note: ChaosMonkey complicates exact verification, but for the
            // static subscribers, the math must hold.
            const int syncSubs = NUM_SUBSCRIBERS / 2;
            const int asyncSubs = NUM_SUBSCRIBERS / 2;

            while (g_running)
            {
                // Burst fire signals
                for (int i = 0; i < BURST_SIZE; ++i)
                {
                    Payload p(msgId++, id);

                    // Update Expectations BEFORE firing to avoid race at shutdown
                    g_expectedSync += syncSubs;
                    g_expectedAsync += asyncSubs;
                    g_signalsFired++;

                    OnMessage(p); // Emit
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            });
    }

    void Join() { if (m_thread.joinable()) m_thread.join(); }

private:
    std::thread m_thread;
};

// --- Subscriber ---
class StressSubscriber
{
public:
    XALLOCATOR

    StressSubscriber(int id)
        : m_id(id), m_thread("SubThread_" + std::to_string(id), MAX_QUEUE_SIZE, FullPolicy::TIMEOUT)
    {
        m_thread.CreateThread();
    }

    ~StressSubscriber() {
        m_thread.ExitThread();
    }

    // Synchronous Handler
    void OnSyncEvent(const Payload& p)
    {
        g_syncReceived++;
        // Prevent optimization without volatile overhead
        std::atomic_thread_fence(std::memory_order_relaxed);
    }

    // Asynchronous Handler
    void OnAsyncEvent(const Payload& p)
    {
        g_asyncReceived++;
        std::atomic_thread_fence(std::memory_order_relaxed);
    }

    Thread& GetThread() { return m_thread; }
    int GetId() const { return m_id; }

private:
    int m_id;
    Thread m_thread;
};

// --- The "Chaos Monkey" ---
// Note: This connects a *volatile* subscriber. We do NOT track expected 
// messages for this subscriber because it connects/disconnects randomly.
// It serves only to test container thread-safety, not data integrity counts.
void ChaosMonkey(std::vector<std::shared_ptr<StressPublisher>>& publishers)
{
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> distPub(0, NUM_PUBLISHERS - 1);

    Thread chaosThread("ChaosThread");
    chaosThread.CreateThread();

    struct VolatileSub {
        XALLOCATOR
            void Handle(const Payload&) { /* No-op */ }
    } vSub;

    ScopedConnection conn;

    while (g_running)
    {
        auto& pub = publishers[distPub(rng)];
        conn = pub->OnMessage.Connect(MakeDelegate(&vSub, &VolatileSub::Handle, chaosThread));

        // Hold connection briefly
        std::this_thread::sleep_for(std::chrono::microseconds(100));

        conn.Disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    chaosThread.ExitThread();
}

int stress_test()
{
    // 1. Enable Memory Leak Checks (Windows Debug Only)
#if defined(CHECK_LEAKS) && defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    std::cout << "==========================================\n";
    std::cout << "   DelegateMQ Correctness & Stress Test   \n";
    std::cout << "==========================================\n";
    std::cout << "Publishers: " << NUM_PUBLISHERS << "\n";
    std::cout << "Subscribers: " << NUM_SUBSCRIBERS << " (" << NUM_SUBSCRIBERS / 2 << " Sync, " << NUM_SUBSCRIBERS / 2 << " Async)\n";
    std::cout << "Duration: " << TEST_DURATION_SECONDS << "s\n";
    std::cout << "Starting..." << std::endl;

    {
        // 1. Setup Publishers
        std::vector<std::shared_ptr<StressPublisher>> publishers;
        for (int i = 0; i < NUM_PUBLISHERS; ++i) {
            publishers.push_back(std::make_shared<StressPublisher>());
        }

        // 2. Setup Subscribers
        std::vector<std::shared_ptr<StressSubscriber>> subscribers;
        std::vector<ScopedConnection> connections;

        for (int i = 0; i < NUM_SUBSCRIBERS; ++i) {
            auto sub = std::make_shared<StressSubscriber>(i);
            subscribers.push_back(sub);

            for (auto& pub : publishers) {
                // Half Sync, Half Async
                if (i % 2 == 0) {
                    connections.push_back(pub->OnMessage.Connect(
                        MakeDelegate(sub.get(), &StressSubscriber::OnSyncEvent)));
                }
                else {
                    connections.push_back(pub->OnMessage.Connect(
                        MakeDelegate(sub.get(), &StressSubscriber::OnAsyncEvent, sub->GetThread())));
                }
            }
        }

        // 3. Start Publishers
        for (int i = 0; i < NUM_PUBLISHERS; ++i) {
            publishers[i]->Start(i);
        }

        // 4. Start Chaos Monkey
        std::thread chaosThread(ChaosMonkey, std::ref(publishers));

        // 5. Monitor Loop
        auto start = std::chrono::steady_clock::now();
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();

            double totalRecv = (double)(g_syncReceived + g_asyncReceived);
            std::cout << "[" << std::setw(3) << elapsed << "s] "
                << "Fired: " << g_signalsFired << " | "
                << "Sync: " << g_syncReceived << " | "
                << "Async: " << g_asyncReceived << " | "
                << "Rate: " << (uint64_t)(totalRecv / (elapsed > 0 ? elapsed : 1)) << " msg/s"
                << std::endl;

            if (elapsed >= TEST_DURATION_SECONDS) break;
        }

        // 6. Shutdown
        std::cout << "\nStopping threads..." << std::endl;
        g_running = false;

        std::cout << "Draining queues (3s)..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));

        for (auto& pub : publishers) pub->Join();
        if (chaosThread.joinable()) chaosThread.join();

        // Drain Async Queues
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Clear containers
        connections.clear();
        subscribers.clear();
        publishers.clear();

    } // End of RAII Scope

    // 7. Final Integrity Verification
    uint64_t actualTotal = g_syncReceived + g_asyncReceived;
    uint64_t expectedTotal = g_expectedSync + g_expectedAsync;

    std::cout << "==========================================\n";
    std::cout << "Final Report:\n";
    std::cout << "Signals Fired:   " << g_signalsFired.load() << "\n";
    std::cout << "------------------------------------------\n";
    std::cout << "Sync Expected:   " << g_expectedSync.load() << "\n";
    std::cout << "Sync Received:   " << g_syncReceived.load() << "\n";
    std::cout << "------------------------------------------\n";
    std::cout << "Async Expected:  " << g_expectedAsync.load() << "\n";
    std::cout << "Async Received:  " << g_asyncReceived.load() << "\n";
    std::cout << "------------------------------------------\n";

    bool pass = (g_syncReceived == g_expectedSync) && (g_asyncReceived == g_expectedAsync);

    if (pass) {
        std::cout << "[SUCCESS] ALL CHECKS PASSED. Integrity verified.\n";
    }
    else {
        std::cout << "[FAILURE] MESSAGE LOSS DETECTED!\n";
        std::cout << "Diff Sync:  " << (int64_t)(g_expectedSync - g_syncReceived) << "\n";
        std::cout << "Diff Async: " << (int64_t)(g_expectedAsync - g_asyncReceived) << "\n";
    }

    return pass ? 0 : 1;
}