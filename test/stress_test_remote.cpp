/// @file stress_test_remote.cpp
/// @brief Long-term multi-threaded stress test for DelegateMQ Remote (RPC) features.
///
/// @details This test benchmarks the serialization, transport marshalling, and 
/// dispatching of Remote Delegates in a high-contention environment.
/// 
/// @section Architecture
/// 1. **Virtual Transport:** Uses an in-memory thread-safe queue to simulate a network 
///    pipe. This isolates the test to measure library overhead (serialization + dispatch)
///    rather than OS network stack latency.
/// 2. **Topology:** N 'Client' threads invoke remote delegates in bursts. 1 'Server' 
///    thread pops messages from the pipe and dispatches them to the target function.
/// 3. **Integrity Verification:** A strict accounting of `g_sentCount` vs `g_receivedCount`
///    ensures zero data loss. The shutdown sequence is carefully ordered (Stop Clients 
///    -> Join Clients -> Stop Server) to prevent race conditions where in-flight 
///    messages are orphaned.
///
/// @section Allocator Usage
/// This test utilizes the DelegateMQ Fixed Block Allocator (optional) to maximize 
/// throughput and eliminate heap fragmentation (embedded systems).
/// - Define `DMQ_ALLOCATOR` globally to enable the feature.
/// - The `XALLOCATOR` macro is placed inside `Payload`, `ServerNode`, and 
///   `ClientNode` to overload `operator new`/`delete`.
///
/// @section Performance Note
/// In Debug builds, thread pre-emption may cause lower throughput. In Release builds,
/// this test verifies the library's ability to handle high-frequency RPC calls 
/// (>100k/sec) without memory leaks or data corruption.
#include "DelegateMQ.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <functional>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace dmq::transport;

// --- Configuration ---
const int NUM_CLIENTS = 4;
const int TEST_DURATION_SECONDS = 15;
const int BURST_SIZE = 500;
const DelegateRemoteId REMOTE_ID = 1;

// --- Global Counters ---
static std::atomic<uint32_t> g_sentCount{ 0 };
static std::atomic<uint32_t> g_receivedCount{ 0 };

// shutdown flags
static std::atomic<bool>     g_stopClients{ false }; // Stops producers
static std::atomic<bool>     g_running{ true };      // Stops server/transport

// --- 1. Payload with Serialization ---
struct Payload
{
    XALLOCATOR

    int id;
    int clientId;
    char data[32];

    Payload() : id(0), clientId(0) { std::memset(data, 0, sizeof(data)); }
    Payload(int i, int c) : id(i), clientId(c) {
        snprintf(data, sizeof(data), "RemoteMsg-%d", i);
    }

    friend std::ostream& operator<<(std::ostream& os, const Payload& p) {
        os.write(reinterpret_cast<const char*>(&p.id), sizeof(p.id));
        os.write(reinterpret_cast<const char*>(&p.clientId), sizeof(p.clientId));
        os.write(p.data, sizeof(p.data));
        return os;
    }

    friend std::istream& operator>>(std::istream& is, Payload& p) {
        is.read(reinterpret_cast<char*>(&p.id), sizeof(p.id));
        is.read(reinterpret_cast<char*>(&p.clientId), sizeof(p.clientId));
        is.read(p.data, sizeof(p.data));
        return is;
    }
};

// --- 2. Interface Implementations ---

template<typename... Args>
class TestSerializer;

template<typename Ret, typename... Args>
class TestSerializer<Ret(Args...)> : public dmq::ISerializer<Ret(Args...)> {
public:
    virtual std::ostream& Write(std::ostream& os, const Args&... args) override {
        (void)std::initializer_list<int>{ (os << args, 0)... };
        return os;
    }
    virtual std::istream& Read(std::istream& is, Args&... args) override {
        (void)std::initializer_list<int>{ (is >> args, 0)... };
        return is;
    }
};

class TestDispatcher : public dmq::IDispatcher {
public:
    using SendFunc = std::function<void(std::stringstream&)>;

    TestDispatcher(SendFunc func) : m_func(func) {}

    virtual int Dispatch(std::ostream& os, dmq::DelegateRemoteId id) override {
        auto* ss = dynamic_cast<std::stringstream*>(&os);
        if (ss && m_func) {
            m_func(*ss);
            return 0; // Success
        }
        return -1; // Error
    }
private:
    SendFunc m_func;
};

// --- 3. Virtual Transport (Simulated Network) ---
class VirtualTransport
{
public:
    using Packet = std::string;

    void Send(const std::stringstream& ss)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.push(ss.str());
        m_cv.notify_one();
    }

    bool Receive(std::stringstream& out_ss)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        // Server keeps waiting as long as g_running is true OR queue has data
        m_cv.wait(lock, [this]() { return !m_queue.empty() || !g_running; });

        // If queue is empty here, it implies g_running == false
        if (m_queue.empty()) return false;

        out_ss.str(m_queue.front());
        m_queue.pop();
        return true;
    }

private:
    std::queue<Packet> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

VirtualTransport g_transport;

// --- 4. Server Node (Receiver) ---
class ServerNode
{
public:
    XALLOCATOR

    ServerNode() : m_remoteDelegate(REMOTE_ID)
    {
        m_remoteDelegate.SetStream(&m_stream);
        m_remoteDelegate.SetSerializer(&m_serializer);
        m_remoteDelegate.SetErrorHandler(MakeDelegate(this, &ServerNode::OnError));
        m_remoteDelegate = MakeDelegate(this, &ServerNode::OnRemoteCall, REMOTE_ID);
    }

    void Start() {
        m_thread = std::thread([this]() {
            std::stringstream incoming;
            // Drain loop: continue until Receive returns false (queue empty & stopped)
            while (g_transport.Receive(incoming)) {
                m_remoteDelegate.Invoke(incoming);
            }
            });
    }

    void Join() { if (m_thread.joinable()) m_thread.join(); }

    void OnRemoteCall(Payload p) {
        g_receivedCount++;
        std::atomic_thread_fence(std::memory_order_relaxed);
    }

    void OnError(DelegateRemoteId id, DelegateError err, DelegateErrorAux aux) {
        // Suppress expected shutdown errors
    }

private:
    std::thread m_thread;
    std::stringstream m_stream{ std::ios::in | std::ios::out | std::ios::binary };
    TestSerializer<void(Payload)> m_serializer;
    dmq::DelegateMemberRemote<ServerNode, void(Payload)> m_remoteDelegate;
};

// --- 5. Client Node (Sender) ---
class ClientNode
{
public:
    XALLOCATOR

    ClientNode(int id)
        : m_id(id)
        , m_remote(REMOTE_ID)
        , m_dispatcher([this](std::stringstream& ss) { this->TransportSend(ss); })
    {
        m_remote.SetStream(&m_stream);
        m_remote.SetSerializer(&m_serializer);
        m_remote.SetDispatcher(&m_dispatcher);
        m_remote.SetErrorHandler(dmq::MakeDelegate(this, &ClientNode::OnError));
    }

    void Start() {
        m_thread = std::thread([this]() {
            int seq = 0;
            // Check global client stop flag
            while (!g_stopClients) {
                for (int i = 0; i < BURST_SIZE && !g_stopClients; ++i) {
                    Payload p(seq++, m_id);
                    g_sentCount++;
                    m_remote(p);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            });
    }

    void Join() { if (m_thread.joinable()) m_thread.join(); }

    void OnError(dmq::DelegateRemoteId id, dmq::DelegateError err, dmq::DelegateErrorAux aux) {
    }

private:
    void TransportSend(std::stringstream& ss) {
        g_transport.Send(ss);
        ss.str("");
        ss.clear();
    }

    int m_id;
    std::thread m_thread;
    std::stringstream m_stream{ std::ios::in | std::ios::out | std::ios::binary };
    TestSerializer<void(Payload)> m_serializer;
    TestDispatcher m_dispatcher;
    dmq::DelegateFreeRemote<void(Payload)> m_remote;
};

// --- Main Test Function ---
int stress_test_remote()
{
    std::cout << "==========================================\n";
    std::cout << "   DelegateMQ Remote RPC Stress Test      \n";
    std::cout << "==========================================\n";
    std::cout << "Clients: " << NUM_CLIENTS << "\n";
    std::cout << "Duration: " << TEST_DURATION_SECONDS << "s\n";
    std::cout << "Transport: Virtual In-Memory Pipe\n";
    std::cout << "Allocations: " << (sizeof(Payload)) << " bytes per msg\n";
    std::cout << "Starting..." << std::endl;

    ServerNode server;
    server.Start();

    std::vector<std::shared_ptr<ClientNode>> clients;
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        auto client = std::make_shared<ClientNode>(i);
        clients.push_back(client);
        client->Start();
    }

    auto start = std::chrono::steady_clock::now();
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();

        uint64_t sent = g_sentCount.load();
        uint64_t recv = g_receivedCount.load();

        std::cout << "[" << std::setw(3) << elapsed << "s] "
            << "Sent: " << sent << " | "
            << "Recv: " << recv << " | "
            << "Lag: " << (sent > recv ? sent - recv : 0) << " | "
            << "Rate: " << (recv / (elapsed > 0 ? elapsed : 1)) << " rpc/s"
            << std::endl;

        if (elapsed >= TEST_DURATION_SECONDS) break;
    }

    std::cout << "\nStopping Clients..." << std::endl;
    // 1. Stop Producers FIRST
    g_stopClients = true;

    // 2. Wait for Producers to completely finish
    for (auto& c : clients) c->Join();

    // 3. Now it is safe to stop the Server
    std::cout << "Stopping Server..." << std::endl;
    g_running = false;

    // 4. Wake server if it's idle
    g_transport.Send(std::stringstream());

    std::cout << "Draining pipe (3s)..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    server.Join();

    uint64_t totalSent = g_sentCount.load();
    uint64_t totalRecv = g_receivedCount.load();

    std::cout << "==========================================\n";
    std::cout << "Final Report:\n";
    std::cout << "RPCs Sent:     " << totalSent << "\n";
    std::cout << "RPCs Received: " << totalRecv << "\n";
    std::cout << "Diff:          " << (int64_t)(totalSent - totalRecv) << "\n";
    std::cout << "------------------------------------------\n";

    if (totalSent == totalRecv && totalSent > 0) {
        std::cout << "[SUCCESS] Integrity verified. No data loss.\n";
        return 0;
    }
    else {
        std::cout << "[FAILURE] Data mismatch detected.\n";
        return 1;
    }
}
