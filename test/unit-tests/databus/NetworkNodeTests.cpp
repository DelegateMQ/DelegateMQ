#include "DelegateMQ.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

#if defined(DMQ_DATABUS)

#include "extras/databus/NetworkNode.h"
#include "extras/util/NetworkConnect.h"

#if defined(_WIN32) || defined(_WIN64)
    #include "port/transport/win32-udp/Win32UdpTransport.h"
    using NetworkNodeTestTransport = dmq::transport::Win32UdpTransport;
#elif defined(__linux__)
    #include "port/transport/linux-udp/LinuxUdpTransport.h"
    using NetworkNodeTestTransport = dmq::transport::LinuxUdpTransport;
#else
    #error "NetworkNodeTests.cpp: no UDP transport available for this platform."
#endif

using namespace dmq;
using namespace dmq::databus;
using namespace dmq::serialization::serializer;

namespace {

// Polls pred() until it returns true or timeout elapses. NetworkNode's receive
// path is driven by a background thread + timer, not synchronously by the
// calling thread, so tests must poll rather than assert immediately -- same
// technique as the retry-poll loop in DataBusRemoteTest.cpp (test #4).
template <typename Pred>
bool WaitFor(Pred pred, std::chrono::milliseconds timeout,
             std::chrono::milliseconds pollInterval = std::chrono::milliseconds(10)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(pollInterval);
    }
    return pred();
}

} // namespace

int NetworkNodeTestMain() {
#if 1
    std::cout << "Starting NetworkNodeTest..." << std::endl;

    // RAII Winsock init/cleanup for the real UDP sockets this test opens.
    // test/main.cpp never constructs one itself (its own examples all use
    // in-process loopback transports), so this test must -- see
    // NetworkConnect.h: "Instantiate this ONCE at the top of main()." A
    // function-local static satisfies that: constructed on this function's
    // first call (across all 3 iterations of main()'s test loop) and torn
    // down once at process exit.
    static dmq::util::NetworkContext networkContext;

    Serializer<void(int)> serializer;

    // ------------------------------------------------------------------
    // 1. End-to-end round trip over a real UDP socket pair on localhost,
    //    UNRELIABLE. Also exercises order-independence: Send() is
    //    registered on NodeA BEFORE Start()/AddPeer(), Receive() is
    //    registered on NodeB AFTER Start() -- both orders the module
    //    README documents as valid.
    //
    // Single-process-test caveat (documented per CLAUDE.md's synchronous-
    // dispatch convention): DataBus is a process-wide singleton, so NodeA
    // and NodeB share ONE DataBus registry even though they represent two
    // separate machines. DataBus::Publish() dispatches to local subscribers
    // SYNCHRONOUSLY before it ever touches the network, so a subscriber to
    // "nn/basic" fires ONCE immediately regardless of the network, and
    // fires a SECOND time later once NodeB's receive thread actually
    // decodes the real UDP packet and calls PublishLocal(). Both firings
    // are expected and asserted below. This doubling is a single-process
    // test artifact only -- a real deployment runs NodeA/NodeB as separate
    // processes with separate DataBus instances, so the sender-side local
    // fan-out never reaches the receiver's subscribers directly.
    // ------------------------------------------------------------------
    {
        DataBus::ResetForTesting();

        NetworkNode<NetworkNodeTestTransport> nodeA;
        NetworkNode<NetworkNodeTestTransport> nodeB;

        // Send() registered before Start()/AddPeer() on the sender.
        nodeA.Send<int>("nn/basic", 1001, serializer, Reliability::UNRELIABLE);

        DMQ_ASSERT_TRUE(nodeA.Start("NodeA_Basic", 15310));
        DMQ_ASSERT_TRUE(nodeB.Start("NodeB_Basic", 15311));

        nodeA.AddPeer("NodeB", "127.0.0.1", 15311);

        // Receive() registered after Start() on the receiver.
        nodeB.Receive<int>("nn/basic", 1001, serializer);

        std::atomic<int> deliveryCount{0};
        std::atomic<int> lastValue{0};
        auto conn = DataBus::Subscribe<int>("nn/basic", [&](int v) {
            deliveryCount++;
            lastValue = v;
        });

        DataBus::Publish<int>("nn/basic", 777);

        // Synchronous local dispatch happens inside Publish() itself.
        DMQ_ASSERT_TRUE(deliveryCount.load() == 1);
        DMQ_ASSERT_TRUE(lastValue.load() == 777);

        // Real network delivery arrives later via NodeB's receive thread.
        bool delivered = WaitFor([&] { return deliveryCount.load() >= 2; }, std::chrono::milliseconds(2000));
        DMQ_ASSERT_TRUE(delivered);
        DMQ_ASSERT_TRUE(lastValue.load() == 777);
    }

    // ------------------------------------------------------------------
    // 2. RELIABLE happy path: message is ACKed over the real link, so
    //    OnPeerSendStatus reports SUCCESS and OnDeliveryFailed never fires.
    // ------------------------------------------------------------------
    {
        DataBus::ResetForTesting();

        NetworkNode<NetworkNodeTestTransport> nodeA;
        NetworkNode<NetworkNodeTestTransport> nodeB;

        DMQ_ASSERT_TRUE(nodeA.Start("NodeA_Reliable", 15312));
        DMQ_ASSERT_TRUE(nodeB.Start("NodeB_Reliable", 15313));

        nodeB.Receive<int>("nn/reliable", 2001, serializer);
        nodeA.AddPeer("NodeB", "127.0.0.1", 15313);
        nodeA.Send<int>("nn/reliable", 2001, serializer, Reliability::RELIABLE);

        std::atomic<int> failedCount{0};
        std::atomic<int> successCount{0};
        auto failConn = nodeA.OnDeliveryFailed.Connect(dmq::MakeDelegate(
            [&](const dmq::xstring&, dmq::DelegateRemoteId, uint16_t) { failedCount++; }));
        auto statusConn = nodeA.OnPeerSendStatus.Connect(dmq::MakeDelegate(
            [&](const dmq::xstring&, dmq::DelegateRemoteId, uint16_t, dmq::util::TransportMonitor::Status status) {
                if (status == dmq::util::TransportMonitor::Status::SUCCESS) successCount++;
            }));

        std::atomic<int> deliveryCount{0};
        auto conn = DataBus::Subscribe<int>("nn/reliable", [&](int) { deliveryCount++; });

        DataBus::Publish<int>("nn/reliable", 42);

        // Synchronous local dispatch.
        DMQ_ASSERT_TRUE(deliveryCount.load() == 1);

        // ACK arrives over the real loopback link well within the 2s
        // default TransportMonitor timeout.
        bool acked = WaitFor([&] { return successCount.load() >= 1; }, std::chrono::milliseconds(2000));
        DMQ_ASSERT_TRUE(acked);
        DMQ_ASSERT_TRUE(failedCount.load() == 0);

        bool delivered = WaitFor([&] { return deliveryCount.load() >= 2; }, std::chrono::milliseconds(2000));
        DMQ_ASSERT_TRUE(delivered);
    }

    // ------------------------------------------------------------------
    // 3. RELIABLE failure path: the peer at the target port never listens,
    //    so no ACK ever arrives. Deterministically exercises the full
    //    retry budget (default DMQ_RETRY_MONITOR_MAX_RETRIES=3) and the
    //    final OnDeliveryFailed signal, plus at least one TIMEOUT
    //    OnPeerSendStatus along the way. Also drives OnPeerCapExceeded via
    //    a burst of sends that all pile up unacknowledged against the same
    //    dead peer. (OnPeerPendingExceeded is intentionally not exercised
    //    here -- reliably forcing Process() to fall behind within a single
    //    batch pass needs much tighter control over Process()'s internal
    //    batch size than NetworkNode's public API exposes.)
    // ------------------------------------------------------------------
    {
        DataBus::ResetForTesting();

        NetworkNode<NetworkNodeTestTransport> nodeA;
        DMQ_ASSERT_TRUE(nodeA.Start("NodeA_Dead", 15314));

        // No one listens on 15399 -- UDP Send() still succeeds at the
        // socket level (no synchronous failure), but no ACK will ever
        // arrive, so the full retry budget must be exhausted.
        nodeA.AddPeer("Ghost", "127.0.0.1", 15399);
        nodeA.Send<int>("nn/dead", 3001, serializer, Reliability::RELIABLE);

        std::atomic<int> failedCount{0};
        std::atomic<int> timeoutCount{0};
        std::atomic<int> capExceededCount{0};
        std::atomic<dmq::DelegateRemoteId> failedId{0};

        auto failConn = nodeA.OnDeliveryFailed.Connect(dmq::MakeDelegate(
            [&](const dmq::xstring&, dmq::DelegateRemoteId id, uint16_t) {
                failedCount++;
                failedId = id;
            }));
        auto statusConn = nodeA.OnPeerSendStatus.Connect(dmq::MakeDelegate(
            [&](const dmq::xstring&, dmq::DelegateRemoteId, uint16_t, dmq::util::TransportMonitor::Status status) {
                if (status == dmq::util::TransportMonitor::Status::TIMEOUT) timeoutCount++;
            }));
        auto capConn = nodeA.OnPeerCapExceeded.Connect(dmq::MakeDelegate(
            [&](const dmq::xstring&, size_t) { capExceededCount++; }));

        DataBus::Publish<int>("nn/dead", 1);

        // Total time to exhaust the default retry budget is roughly
        // (DMQ_RETRY_MONITOR_MAX_RETRIES + 1) * DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC
        // seconds -- 4 * 2s = 8s with the shipped defaults. Poll generously.
        bool failed = WaitFor([&] { return failedCount.load() >= 1; },
                               std::chrono::milliseconds(15000), std::chrono::milliseconds(50));
        DMQ_ASSERT_TRUE(failed);
        DMQ_ASSERT_TRUE(failedId.load() == 3001);
        DMQ_ASSERT_TRUE(timeoutCount.load() >= 1);

        // Flood past MAX_TRANSPORT_MONITOR_PENDING (default 100) with more
        // RELIABLE sends to the same dead peer, all still pending -- forces
        // TransportMonitor::OnCapExceeded (relayed as OnPeerCapExceeded).
        // Paced in batches rather than one tight loop: AddPeer() routes each
        // Publish() through SetSendThread(&*m_thread) (NetworkNode.h), so
        // this burst also queues onto NodeA's own worker thread, which has
        // an unrelated, smaller, unconditional-FAULT queue cap (100) of its
        // own -- firing all 120 without pause can trip that cap first.
        for (int batch = 0; batch < 6; ++batch) {
            for (int i = 0; i < 20; ++i) {
                DataBus::Publish<int>("nn/dead", batch * 20 + i);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        bool capHit = WaitFor([&] { return capExceededCount.load() >= 1; }, std::chrono::milliseconds(2000));
        DMQ_ASSERT_TRUE(capHit);
    }

    std::cout << "NetworkNodeTest PASSED!" << std::endl;
#endif
    return 0;
}

#else

int NetworkNodeTestMain() {
    return 0;
}

#endif
