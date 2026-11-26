/// @file ShmRingTransportTests.cpp
/// @brief Unit tests for port/transport/shm-ring/ShmRingTransport.h, in one
/// process: the two sides share an ordinary ShmControlBlock, and a condition
/// variable stands in for the cross-core interrupt (IShmSignal).
///
/// Covers: both directions; an empty ring; a full ring and recovery; 32-bit
/// index wrap-around; payload limits; a malformed slot; attaching side B
/// (uninitialized block, mismatched layout); the signal waking a blocked
/// receiver, and the receive timeout; ACKs clearing a TransportMonitor and a
/// silent peer timing out; a remote delegate call end to end; and multithreaded
/// stress (both directions at once, two senders on one side) checking order
/// and contents.
///
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.

#include "DelegateMQ.h"
#include "port/transport/shm-ring/ShmRingTransport.h"
#include "UnitTestCommon.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace dmq;
using namespace dmq::transport;
using namespace std::chrono_literals;

namespace {

/// One side's doorbell: NotifyPeer() sets the peer's latched flag, WaitForData()
/// waits on this side's. Stands in for an HSEM interrupt + semaphore.
class TestSignal : public IShmSignal
{
public:
    TestSignal* peer = nullptr;
    std::atomic<int> notifyCount{ 0 };

    void NotifyPeer() override
    {
        notifyCount.fetch_add(1);
        if (peer) peer->Set();
    }

    bool WaitForData(std::chrono::milliseconds timeout) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, timeout, [this] { return m_flag; });
        const bool notified = m_flag;
        m_flag = false;
        return notified;
    }

    void Set()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_flag = true;
        }
        m_cv.notify_one();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_flag = false;
};

/// Both sides of a link over one block, with their signals paired up.
template <size_t SlotCount, size_t MaxPayload>
struct Link
{
    using Shm = ShmRingTransport<SlotCount, MaxPayload>;
    // Heap: the block can be large, and its alignment is honoured by new (C++17)
    std::unique_ptr<typename Shm::ControlBlock> block = std::make_unique<typename Shm::ControlBlock>();
    TestSignal sigA, sigB;
    Shm a, b;

    explicit Link(bool withSignal = true)
    {
        sigA.peer = &sigB;
        sigB.peer = &sigA;
        DMQ_ASSERT_TRUE(a.Create(*block, Shm::Side::A, withSignal ? &sigA : nullptr) == 0);
        DMQ_ASSERT_TRUE(b.Create(*block, Shm::Side::B, withSignal ? &sigB : nullptr) == 0);
    }
};

int SendBytes(ITransport& t, uint16_t id, uint16_t seq, const void* data, size_t len)
{
    ByteWriter os;
    if (len > 0)
        os.write(static_cast<const char*>(data), len);
    return t.Send(os, DmqHeader(id, seq));
}

int SendU32(ITransport& t, uint16_t id, uint16_t seq, uint32_t value)
{
    return SendBytes(t, id, seq, &value, sizeof(value));
}

/// Receive one message carrying a uint32_t.
int RecvU32(ITransport& t, DmqHeader& header, uint32_t& value)
{
    ByteReader is;
    const int rc = t.Receive(is, header);
    if (rc != 0) return rc;
    if (is.remaining() != sizeof(value)) return -100;
    is.read(reinterpret_cast<char*>(&value), sizeof(value));
    return 0;
}

// Messages arrive in order with their header fields and bytes, both directions
void ShmRing_SendReceive_BothDirections()
{
    Link<8, 64> link(false);
    link.a.SetSendTransport(nullptr);   // no ACKs: this test reads every message itself
    link.b.SetSendTransport(nullptr);

    DMQ_ASSERT_TRUE(SendU32(link.a, 10, 1, 111) == 0);
    DMQ_ASSERT_TRUE(SendU32(link.a, 11, 2, 222) == 0);
    DMQ_ASSERT_TRUE(SendU32(link.b, 20, 7, 777) == 0);

    DmqHeader h;
    uint32_t v = 0;
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0);
    DMQ_ASSERT_TRUE(h.GetId() == 10 && h.GetSeqNum() == 1 && h.GetLength() == 4 && v == 111);
    DMQ_ASSERT_TRUE(h.GetNodeId() == DmqHeader::GetLocalNodeId());
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0);
    DMQ_ASSERT_TRUE(h.GetId() == 11 && h.GetSeqNum() == 2 && v == 222);
    DMQ_ASSERT_TRUE(RecvU32(link.a, h, v) == 0);
    DMQ_ASSERT_TRUE(h.GetId() == 20 && h.GetSeqNum() == 7 && v == 777);

    ByteReader is;
    DMQ_ASSERT_TRUE(link.a.Receive(is, h) == ITransport::RECEIVE_NO_DATA);
    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == ITransport::RECEIVE_NO_DATA);
    DMQ_ASSERT_TRUE(is.remaining() == 0);
    std::cout << "ShmRing_SendReceive_BothDirections() complete!" << std::endl;
}

// A full ring fails Send() and counts it; after a receive there is room again
void ShmRing_FullRing_FailsAndRecovers()
{
    Link<4, 16> link(false);
    link.b.SetSendTransport(nullptr);

    for (uint16_t i = 0; i < 4; i++)
        DMQ_ASSERT_TRUE(SendU32(link.a, 1, i, i) == 0);
    DMQ_ASSERT_TRUE(SendU32(link.a, 1, 99, 99) == -1);
    DMQ_ASSERT_TRUE(link.a.GetSendFullCount() == 1);

    DmqHeader h;
    uint32_t v = 0;
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0 && v == 0);
    DMQ_ASSERT_TRUE(SendU32(link.a, 1, 4, 4) == 0);          // one slot free again
    for (uint32_t expect = 1; expect <= 4; expect++) {
        DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0);
        DMQ_ASSERT_TRUE(v == expect && h.GetSeqNum() == expect);
    }
    ByteReader is;
    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == ITransport::RECEIVE_NO_DATA);
    std::cout << "ShmRing_FullRing_FailsAndRecovers() complete!" << std::endl;
}

// The free-running indices wrap past 2^32 without losing or reordering anything
void ShmRing_IndexWrap_32Bit()
{
    Link<4, 16> link(false);
    link.b.SetSendTransport(nullptr);
    // Both sides idle, so the test may move the indices of the A->B ring
    link.block->aToB.head.store(0xFFFFFFFEu);
    link.block->aToB.tail.store(0xFFFFFFFEu);

    DmqHeader h;
    uint32_t v = 0;
    uint32_t next = 0;
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < 3; i++)
            DMQ_ASSERT_TRUE(SendU32(link.a, 1, 0, next + i) == 0);
        for (int i = 0; i < 3; i++) {
            DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0);
            DMQ_ASSERT_TRUE(v == next + i);
        }
        next += 3;
    }
    DMQ_ASSERT_TRUE(link.block->aToB.head.load() < 0x100u);  // wrapped
    // Full detection across the wrap: 4 fit, the 5th doesn't
    for (int i = 0; i < 4; i++)
        DMQ_ASSERT_TRUE(SendU32(link.a, 1, 0, 0) == 0);
    DMQ_ASSERT_TRUE(SendU32(link.a, 1, 0, 0) == -1);
    std::cout << "ShmRing_IndexWrap_32Bit() complete!" << std::endl;
}

// MaxPayload bytes fit, one more fails and is counted; an empty payload works
void ShmRing_PayloadLimits()
{
    Link<4, 32> link(false);
    link.b.SetSendTransport(nullptr);

    uint8_t big[33];
    for (size_t i = 0; i < sizeof(big); i++) big[i] = static_cast<uint8_t>(i + 1);

    DMQ_ASSERT_TRUE(SendBytes(link.a, 5, 1, big, 33) == -1);
    DMQ_ASSERT_TRUE(link.a.GetSendTooLargeCount() == 1);
    DMQ_ASSERT_TRUE(SendBytes(link.a, 5, 2, big, 32) == 0);
    DMQ_ASSERT_TRUE(SendBytes(link.a, 5, 3, nullptr, 0) == 0);

    DmqHeader h;
    ByteReader is;
    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == 0);
    DMQ_ASSERT_TRUE(h.GetSeqNum() == 2 && is.remaining() == 32);
    uint8_t got[32] = {};
    is.read(reinterpret_cast<char*>(got), sizeof(got));
    DMQ_ASSERT_TRUE(memcmp(got, big, 32) == 0);

    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == 0);
    DMQ_ASSERT_TRUE(h.GetSeqNum() == 3 && h.GetLength() == 0 && is.remaining() == 0);
    std::cout << "ShmRing_PayloadLimits() complete!" << std::endl;
}

// A slot with a bad marker or an impossible length is dropped and counted; the
// next message is unaffected
void ShmRing_MalformedSlot_DroppedAndCounted()
{
    Link<4, 16> link(false);
    link.b.SetSendTransport(nullptr);

    DMQ_ASSERT_TRUE(SendU32(link.a, 1, 1, 1) == 0);
    DMQ_ASSERT_TRUE(SendU32(link.a, 1, 2, 2) == 0);
    DMQ_ASSERT_TRUE(SendU32(link.a, 1, 3, 3) == 0);
    link.block->aToB.slots[0].header[0] = 0x00;              // corrupt the marker
    DmqHeader bad(1, 2, 17);                                 // length > MaxPayload
    bad.Encode(link.block->aToB.slots[1].header);

    DmqHeader h;
    uint32_t v = 0;
    ByteReader is;
    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == -1);
    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == -1);
    DMQ_ASSERT_TRUE(link.b.GetRecvErrorCount() == 2);
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0 && v == 3);
    std::cout << "ShmRing_MalformedSlot_DroppedAndCounted() complete!" << std::endl;
}

// Side B can't attach before side A initialized the block, or to a block with
// another layout; IsPeerOpen() follows each side's Create()/Close()
void ShmRing_Attach()
{
    using Shm = ShmRingTransport<4, 16>;
    auto block = std::make_unique<Shm::ControlBlock>();
    Shm a, b;

    DMQ_ASSERT_TRUE(b.Create(*block, Shm::Side::B) == -1);   // not initialized yet
    DMQ_ASSERT_TRUE(!b.IsOpen());
    ByteWriter os;
    DmqHeader h;
    ByteReader is;
    DMQ_ASSERT_TRUE(b.Send(os, DmqHeader(1, 1)) == -1);      // closed
    DMQ_ASSERT_TRUE(b.Receive(is, h) == -1);

    DMQ_ASSERT_TRUE(a.Create(*block, Shm::Side::A) == 0);
    DMQ_ASSERT_TRUE(a.IsOpen() && !a.IsPeerOpen());

    block->maxPayload = 32;                                   // as if built differently
    DMQ_ASSERT_TRUE(b.Create(*block, Shm::Side::B) == -1);
    block->maxPayload = 16;
    DMQ_ASSERT_TRUE(b.Create(*block, Shm::Side::B) == 0);
    DMQ_ASSERT_TRUE(a.IsPeerOpen() && b.IsPeerOpen());

    // A message sent before the peer reads survives the peer closing and reopening
    DMQ_ASSERT_TRUE(SendU32(a, 1, 1, 42) == 0);
    b.Close();
    DMQ_ASSERT_TRUE(!a.IsPeerOpen() && !b.IsOpen());
    DMQ_ASSERT_TRUE(b.Create(*block, Shm::Side::B) == 0);
    b.SetSendTransport(nullptr);
    uint32_t v = 0;
    DMQ_ASSERT_TRUE(RecvU32(b, h, v) == 0 && v == 42);
    std::cout << "ShmRing_Attach() complete!" << std::endl;
}

// A receiver blocked in Receive() wakes when the peer sends; with nothing sent
// it returns RECEIVE_NO_DATA after the receive timeout
void ShmRing_Signal_WakesReceiver()
{
    Link<4, 16> link(true);
    link.b.SetSendTransport(nullptr);
    link.b.SetRecvTimeout(5000ms);

    std::atomic<int> rc{ -99 };
    std::atomic<uint32_t> value{ 0 };
    const auto start = std::chrono::steady_clock::now();
    std::thread receiver([&] {
        DmqHeader h;
        uint32_t v = 0;
        rc = RecvU32(link.b, h, v);
        value = v;
    });
    std::this_thread::sleep_for(50ms);
    DMQ_ASSERT_TRUE(SendU32(link.a, 1, 1, 1234) == 0);
    receiver.join();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    DMQ_ASSERT_TRUE(rc == 0 && value == 1234);
    DMQ_ASSERT_TRUE(elapsed < 2000ms);                       // woken, not timed out
    DMQ_ASSERT_TRUE(link.sigA.notifyCount == 1);

    // Nothing sent: the wait ends at the timeout. (Margin: Windows timer ticks
    // are ~15.6 ms, and a wait may end up to a tick early.)
    link.b.SetRecvTimeout(60ms);
    DmqHeader h;
    ByteReader is;
    auto t0 = std::chrono::steady_clock::now();
    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == ITransport::RECEIVE_NO_DATA);
    DMQ_ASSERT_TRUE(std::chrono::steady_clock::now() - t0 >= 40ms);

    // A message taken without waiting leaves the signal set. The next empty
    // Receive() must still wait out its timeout, not return at once.
    DMQ_ASSERT_TRUE(SendU32(link.a, 1, 2, 5678) == 0);       // sets B's signal
    uint32_t v = 0;
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0 && v == 5678); // ring not empty: no wait
    t0 = std::chrono::steady_clock::now();
    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == ITransport::RECEIVE_NO_DATA);
    DMQ_ASSERT_TRUE(std::chrono::steady_clock::now() - t0 >= 40ms);

    // Timeout 0: never blocks, even with a signal
    link.b.SetRecvTimeout(0ms);
    DMQ_ASSERT_TRUE(link.b.Receive(is, h) == ITransport::RECEIVE_NO_DATA);
    std::cout << "ShmRing_Signal_WakesReceiver() complete!" << std::endl;
}

// A received message is ACKed back; the ACK clears the sender's TransportMonitor
// entry (SUCCESS). With no reply, the entry times out (TIMEOUT): a dead peer
void ShmRing_Ack_TransportMonitor()
{
    Link<8, 16> link(false);
    dmq::util::TransportMonitor monitor(std::chrono::duration_cast<dmq::Duration>(std::chrono::milliseconds(20)));
    link.a.SetTransportMonitor(&monitor);

    std::atomic<int> success{ 0 }, timeout{ 0 };
    auto conn = monitor.OnSendStatus.Connect(dmq::MakeDelegate(
        [&](DelegateRemoteId, uint16_t seq, dmq::util::TransportMonitor::Status status) {
            if (status == dmq::util::TransportMonitor::Status::SUCCESS && seq == 5) success++;
            if (status == dmq::util::TransportMonitor::Status::TIMEOUT && seq == 6) timeout++;
        }));

    // Tracked as RetryMonitor would: Add(), then Send()
    DMQ_ASSERT_TRUE(monitor.Add(5, 30));
    DMQ_ASSERT_TRUE(SendU32(link.a, 30, 5, 555) == 0);
    DmqHeader h;
    uint32_t v = 0;
    DMQ_ASSERT_TRUE(RecvU32(link.b, h, v) == 0 && v == 555); // B sends the ACK
    ByteReader is;
    DMQ_ASSERT_TRUE(link.a.Receive(is, h) == 0);              // A takes the ACK
    DMQ_ASSERT_TRUE(h.GetId() == dmq::ACK_REMOTE_ID && h.GetSeqNum() == 5);
    DMQ_ASSERT_TRUE(success == 1);

    // Peer never reads: no ACK, so the entry times out
    DMQ_ASSERT_TRUE(monitor.Add(6, 30));
    DMQ_ASSERT_TRUE(SendU32(link.a, 30, 6, 666) == 0);
    std::this_thread::sleep_for(60ms);
    monitor.Process();
    DMQ_ASSERT_TRUE(timeout == 1);
    std::cout << "ShmRing_Ack_TransportMonitor() complete!" << std::endl;
}

struct Point { int32_t x; int32_t y; };
std::atomic<int> s_pointCalls{ 0 };
std::atomic<int32_t> s_pointSum{ 0 };
void OnPoint(Point p) { s_pointCalls++; s_pointSum += p.x + p.y; }

// A remote delegate call from side A runs the target on side B's receive thread
void ShmRing_RemoteDelegate_EndToEnd()
{
    constexpr DelegateRemoteId POINT_ID = 77;
    Link<16, 64> link(true);
    link.b.SetRecvTimeout(20ms);
    link.a.SetRecvTimeout(0ms);         // A only drains ACKs between sends: don't block
    s_pointCalls = 0;
    s_pointSum = 0;

    dmq::serialization::raw::Serializer<void(Point)> ser;
    RemoteChannel<void(Point)> sender(link.a, ser, POINT_ID);
    DelegateFreeRemote<void(Point)> receiver(&OnPoint, POINT_ID);
    receiver.SetSerializer(&ser);

    constexpr int COUNT = 200;
    std::atomic<bool> stop{ false };
    std::thread recvThread([&] {
        while (!stop) {
            DmqHeader h;
            ByteReader is;
            if (link.b.Receive(is, h) == 0 && h.GetId() == POINT_ID)
                receiver.Invoke(is);
        }
    });

    // A full ring fails the send and is reported here; the loop below resends
    std::atomic<int> sendErrors{ 0 };
    sender.SetErrorHandler(MakeDelegate([&](DelegateRemoteId, DelegateError e, DelegateErrorAux) {
        if (e != DelegateError::SUCCESS) sendErrors++;      // also called on success
    }));

    for (int i = 0; i < COUNT; i++) {
        for (;;) {
            const int errorsBefore = sendErrors;
            sender(Point{ i, 1 });
            if (sendErrors == errorsBefore) break;
            std::this_thread::yield();
        }
        // Drain B's ACKs on A, or they would fill the B->A ring
        DmqHeader h;
        ByteReader is;
        while (link.a.Receive(is, h) == 0) {}
    }

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (s_pointCalls < COUNT && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    stop = true;
    recvThread.join();

    DMQ_ASSERT_TRUE(s_pointCalls == COUNT);
    DMQ_ASSERT_TRUE(s_pointSum == (COUNT - 1) * COUNT / 2 + COUNT);
    DMQ_ASSERT_TRUE(sendErrors == static_cast<int>(link.a.GetSendFullCount()));
    std::cout << "ShmRing_RemoteDelegate_EndToEnd() complete!" << std::endl;
}

/// Payload for the stress tests: a sender id, a sequence, and filler derived
/// from both so a torn or mixed-up slot is caught.
struct StressMsg
{
    uint32_t sender;
    uint32_t seq;
    uint32_t fill[6];

    static StressMsg Make(uint32_t sender, uint32_t seq)
    {
        StressMsg m{ sender, seq, {} };
        for (uint32_t i = 0; i < 6; i++) m.fill[i] = (sender * 0x9E3779B9u) ^ (seq + i);
        return m;
    }
    bool Check() const
    {
        for (uint32_t i = 0; i < 6; i++)
            if (fill[i] != ((sender * 0x9E3779B9u) ^ (seq + i))) return false;
        return true;
    }
};

template <class Shm>
void StressSend(Shm& t, uint32_t sender, uint32_t count)
{
    for (uint32_t seq = 0; seq < count; seq++) {
        const StressMsg m = StressMsg::Make(sender, seq);
        while (SendBytes(t, 1, static_cast<uint16_t>(seq), &m, sizeof(m)) != 0)
            std::this_thread::yield();                         // ring full: wait for room
    }
}

/// Receive `total` messages; each sender's sequence must arrive in order and intact.
template <class Shm>
bool StressReceive(Shm& t, uint32_t total, uint32_t senders)
{
    std::vector<uint32_t> next(senders, 0);
    uint32_t received = 0;
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    while (received < total) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        DmqHeader h;
        ByteReader is;
        const int rc = t.Receive(is, h);
        if (rc == ITransport::RECEIVE_NO_DATA) continue;
        if (rc != 0 || is.remaining() != sizeof(StressMsg)) return false;
        StressMsg m;
        is.read(reinterpret_cast<char*>(&m), sizeof(m));
        if (m.sender >= senders || m.seq != next[m.sender] || !m.Check()) return false;
        if (h.GetSeqNum() != static_cast<uint16_t>(m.seq)) return false;
        next[m.sender]++;
        received++;
    }
    return true;
}

// Both directions at once, a small ring (often full), signal-driven receivers
void ShmRing_Stress_Bidirectional()
{
    constexpr uint32_t COUNT = 100000;
    Link<8, 64> link(true);
    link.a.SetSendTransport(nullptr);
    link.b.SetSendTransport(nullptr);
    link.a.SetRecvTimeout(10ms);
    link.b.SetRecvTimeout(10ms);

    std::atomic<bool> okA{ false }, okB{ false };
    std::thread sendA([&] { StressSend(link.a, 0, COUNT); });
    std::thread sendB([&] { StressSend(link.b, 0, COUNT); });
    std::thread recvA([&] { okA = StressReceive(link.a, COUNT, 1); });
    std::thread recvB([&] { okB = StressReceive(link.b, COUNT, 1); });
    sendA.join(); sendB.join(); recvA.join(); recvB.join();

    DMQ_ASSERT_TRUE(okA && okB);
    DMQ_ASSERT_TRUE(link.a.GetRecvErrorCount() == 0 && link.b.GetRecvErrorCount() == 0);
    std::cout << "ShmRing_Stress_Bidirectional() complete!" << std::endl;
}

// Two threads sending on side A share the ring through its send lock
void ShmRing_Stress_TwoSendersOneSide()
{
    constexpr uint32_t COUNT = 50000;
    Link<8, 64> link(true);
    link.b.SetSendTransport(nullptr);
    link.b.SetRecvTimeout(10ms);

    std::atomic<bool> ok{ false };
    std::thread s0([&] { StressSend(link.a, 0, COUNT); });
    std::thread s1([&] { StressSend(link.a, 1, COUNT); });
    std::thread r([&] { ok = StressReceive(link.b, 2 * COUNT, 2); });
    s0.join(); s1.join(); r.join();

    DMQ_ASSERT_TRUE(ok);
    std::cout << "ShmRing_Stress_TwoSendersOneSide() complete!" << std::endl;
}

} // namespace

void ShmRingTransportTests()
{
    ShmRing_SendReceive_BothDirections();
    ShmRing_FullRing_FailsAndRecovers();
    ShmRing_IndexWrap_32Bit();
    ShmRing_PayloadLimits();
    ShmRing_MalformedSlot_DroppedAndCounted();
    ShmRing_Attach();
    ShmRing_Signal_WakesReceiver();
    ShmRing_Ack_TransportMonitor();
    ShmRing_RemoteDelegate_EndToEnd();
    ShmRing_Stress_Bidirectional();
    ShmRing_Stress_TwoSendersOneSide();
    std::cout << "ShmRingTransportTests() complete!" << std::endl;
}
