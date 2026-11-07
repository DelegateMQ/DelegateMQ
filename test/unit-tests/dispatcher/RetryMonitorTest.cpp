#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include "extras/util/RetryMonitor.h"
#include "extras/util/TransportMonitor.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace std;
using namespace dmq;
using namespace dmq::transport;
using namespace dmq::util;

namespace {
    // Transport that always accepts the send but never delivers an ACK back to
    // TransportMonitor — simulates a peer that never responds, forcing
    // RetryMonitor to exhaust its retry budget.
    class NeverAckTransport : public ITransport {
    public:
        int sendCount = 0;
        int Send(xostringstream&, const DmqHeader&) override {
            sendCount++;
            return 0;
        }
        int Receive(xstringstream&, DmqHeader&) override { return -1; }
    };

    // Transport whose Send() fails synchronously every time — simulates a
    // peer that is down at send time (connection refused, socket error).
    class AlwaysFailTransport : public ITransport {
    public:
        int sendCount = 0;
        int Send(xostringstream&, const DmqHeader&) override {
            sendCount++;
            return -1;
        }
        int Receive(xstringstream&, DmqHeader&) override { return -1; }
    };

    // Transport whose initial Send() succeeds but whose retry-resend fails
    // synchronously — simulates a peer going down between the first send and
    // its retry (e.g. connection dropped mid-timeout window).
    class FailOnRetryTransport : public ITransport {
    public:
        int sendCount = 0;
        int Send(xostringstream&, const DmqHeader&) override {
            sendCount++;
            return sendCount == 1 ? 0 : -1;
        }
        int Receive(xstringstream&, DmqHeader&) override { return -1; }
    };

    // Transport whose first Send() blocks until released, so a TIMEOUT can fire
    // while the initial send is still in progress.
    class BlockingFirstSendTransport : public ITransport {
    public:
        explicit BlockingFirstSendTransport(int firstResult) : m_firstResult(firstResult) {}
        std::atomic<int> sendCount{ 0 };
        std::atomic<bool> inSend{ false };
        std::atomic<bool> release{ false };
        int Send(xostringstream&, const DmqHeader&) override {
            if (sendCount.fetch_add(1) == 0) {
                inSend.store(true);
                while (!release.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return m_firstResult;
            }
            return 0;
        }
        int Receive(xstringstream&, DmqHeader&) override { return -1; }
    private:
        int m_firstResult;
    };

    // TIMEOUT arrives while the first Send() is still running, and RetryMonitor's
    // re-registration with TransportMonitor fails. The entry must be abandoned and
    // reported exactly once, even if that first Send() then fails too.
    void ReregisterFailure(int firstSendResult)
    {
        BlockingFirstSendTransport transport(firstSendResult);
        TransportMonitor monitor(std::chrono::milliseconds(20));
        DmqHeader header(/*id=*/21, /*seqNum=*/5);

        // Connected before RetryMonitor's own handler, so it runs first: it takes
        // the key back in TransportMonitor, making RetryMonitor's re-Add() collide.
        auto blockConn = monitor.OnSendStatus.Connect(dmq::MakeDelegate(
            [&monitor, &header](DelegateRemoteId id, uint16_t seq, TransportMonitor::Status status) {
                if (status == TransportMonitor::Status::TIMEOUT && id == header.GetId() && seq == header.GetSeqNum())
                    monitor.Add(seq, id);
            }));

        RetryMonitor retry;
        retry.Init(transport, monitor, /*maxRetries=*/2);

        std::atomic<int> failedCount{ 0 };
        auto conn = retry.OnDeliveryFailed.Connect(dmq::MakeDelegate(
            [&failedCount](DelegateRemoteId, uint16_t) { failedCount.fetch_add(1); }));

        int err = 0;
        std::thread sender([&]() {
            xostringstream os;
            os << "payload";
            err = retry.SendWithRetry(os, header);
        });

        while (!transport.inSend.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();               // TIMEOUT -> re-register fails
        DMQ_ASSERT_TRUE(failedCount.load() == 1);

        transport.release.store(true);
        sender.join();
        DMQ_ASSERT_TRUE(err == firstSendResult);
        DMQ_ASSERT_TRUE(failedCount.load() == 1);   // not reported twice

        // The entry is gone: the same key can be sent again.
        monitor.Cancel(header.GetSeqNum(), header.GetId());
        xostringstream os2;
        os2 << "payload";
        DMQ_ASSERT_TRUE(retry.SendWithRetry(os2, header) == 0);
        DMQ_ASSERT_TRUE(failedCount.load() == 1);
    }
}

void RetryMonitorTests()
{
    // OnDeliveryFailed fires exactly once, only after the retry budget is
    // exhausted — not on any of the intermediate retry timeouts.
    {
        NeverAckTransport transport;
        TransportMonitor monitor(std::chrono::milliseconds(20));
        RetryMonitor retry(transport, monitor, /*maxRetries=*/2);

        int failedCount = 0;
        DelegateRemoteId failedId = 0;
        uint16_t failedSeq = 0;
        auto conn = retry.OnDeliveryFailed.Connect(dmq::MakeDelegate(
            [&](DelegateRemoteId id, uint16_t seq) {
                failedCount++;
                failedId = id;
                failedSeq = seq;
            }));

        xostringstream os;
        os << "payload";
        DmqHeader header(/*id=*/42, /*seqNum=*/7);

        int err = retry.SendWithRetry(os, header);
        DMQ_ASSERT_TRUE(err == 0);
        DMQ_ASSERT_TRUE(transport.sendCount == 1);
        DMQ_ASSERT_TRUE(failedCount == 0);

        // Timeout #1: 2 attempts remaining -> retry (send #2).
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();
        DMQ_ASSERT_TRUE(transport.sendCount == 2);
        DMQ_ASSERT_TRUE(failedCount == 0);

        // Timeout #2: 1 attempt remaining -> retry (send #3).
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();
        DMQ_ASSERT_TRUE(transport.sendCount == 3);
        DMQ_ASSERT_TRUE(failedCount == 0);

        // Timeout #3: 0 attempts remaining -> retry budget exhausted.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();
        DMQ_ASSERT_TRUE(failedCount == 1);
        DMQ_ASSERT_TRUE(failedId == 42);
        DMQ_ASSERT_TRUE(failedSeq == 7);

        // No further sends or failure signals after exhaustion.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();
        DMQ_ASSERT_TRUE(transport.sendCount == 3);
        DMQ_ASSERT_TRUE(failedCount == 1);
    }

    // A message that IS acknowledged before timing out never reports failure.
    {
        NeverAckTransport transport;
        TransportMonitor monitor(std::chrono::milliseconds(20));
        RetryMonitor retry(transport, monitor, /*maxRetries=*/2);

        int failedCount = 0;
        auto conn = retry.OnDeliveryFailed.Connect(dmq::MakeDelegate(
            [&](DelegateRemoteId, uint16_t) { failedCount++; }));

        xostringstream os;
        os << "payload";
        DmqHeader header(/*id=*/99, /*seqNum=*/3);

        DMQ_ASSERT_TRUE(retry.SendWithRetry(os, header) == 0);
        monitor.Remove(header.GetSeqNum(), header.GetId()); // ACK arrives

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();
        DMQ_ASSERT_TRUE(transport.sendCount == 1); // no retry
        DMQ_ASSERT_TRUE(failedCount == 0);
    }

    // A synchronous Send() failure on the very first attempt must report
    // OnDeliveryFailed immediately, not vanish silently — this is the
    // fast/common failure path (peer already down at send time), distinct
    // from the retry-exhaustion path exercised above.
    {
        AlwaysFailTransport transport;
        TransportMonitor monitor(std::chrono::milliseconds(20));
        RetryMonitor retry(transport, monitor, /*maxRetries=*/2);

        int failedCount = 0;
        DelegateRemoteId failedId = 0;
        uint16_t failedSeq = 0;
        auto conn = retry.OnDeliveryFailed.Connect(dmq::MakeDelegate(
            [&](DelegateRemoteId id, uint16_t seq) {
                failedCount++;
                failedId = id;
                failedSeq = seq;
            }));

        xostringstream os;
        os << "payload";
        DmqHeader header(/*id=*/55, /*seqNum=*/11);

        int err = retry.SendWithRetry(os, header);
        DMQ_ASSERT_TRUE(err != 0);
        DMQ_ASSERT_TRUE(transport.sendCount == 1);
        DMQ_ASSERT_TRUE(failedCount == 1);
        DMQ_ASSERT_TRUE(failedId == 55);
        DMQ_ASSERT_TRUE(failedSeq == 11);

        // No timeout-driven retry follows an immediate failure.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();
        DMQ_ASSERT_TRUE(transport.sendCount == 1);
        DMQ_ASSERT_TRUE(failedCount == 1);

        // TransportMonitor::Add() DID succeed before Send() failed, so this seqNum
        // is registered in TransportMonitor's pending map. RetryMonitor must have
        // cancelled it immediately (not left it to occupy a pending slot until
        // TRANSPORT_TIMEOUT) -- re-Add()'ing the same seqNum must succeed, not hit
        // the "wraparound collision" rejection an orphaned entry would cause.
        DMQ_ASSERT_TRUE(monitor.Add(header.GetSeqNum(), header.GetId()));
    }

    // A synchronous Send() failure on a retry-resend (initial send succeeded,
    // but the peer went down before the retry) must report OnDeliveryFailed
    // immediately too, and must not leave TransportMonitor's pending slot
    // occupied — same reasoning as the initial-send case above, just reached
    // via the OnStatusChanged() retry path instead of SendWithRetry() directly.
    {
        FailOnRetryTransport transport;
        TransportMonitor monitor(std::chrono::milliseconds(20));
        RetryMonitor retry(transport, monitor, /*maxRetries=*/2);

        int failedCount = 0;
        DelegateRemoteId failedId = 0;
        uint16_t failedSeq = 0;
        auto conn = retry.OnDeliveryFailed.Connect(dmq::MakeDelegate(
            [&](DelegateRemoteId id, uint16_t seq) {
                failedCount++;
                failedId = id;
                failedSeq = seq;
            }));

        xostringstream os;
        os << "payload";
        DmqHeader header(/*id=*/77, /*seqNum=*/13);

        int err = retry.SendWithRetry(os, header);
        DMQ_ASSERT_TRUE(err == 0); // initial send succeeds
        DMQ_ASSERT_TRUE(transport.sendCount == 1);
        DMQ_ASSERT_TRUE(failedCount == 0);

        // Timeout fires -> retry-resend attempted -> that Send() fails.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();
        DMQ_ASSERT_TRUE(transport.sendCount == 2);
        DMQ_ASSERT_TRUE(failedCount == 1);
        DMQ_ASSERT_TRUE(failedId == 77);
        DMQ_ASSERT_TRUE(failedSeq == 13);

        // No further retries after the resend failure.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();
        DMQ_ASSERT_TRUE(transport.sendCount == 2);
        DMQ_ASSERT_TRUE(failedCount == 1);

        // Pending slot was freed immediately, not left to expire naturally.
        DMQ_ASSERT_TRUE(monitor.Add(header.GetSeqNum(), header.GetId()));
    }

    // A second send with the same (id, seq) while the first is still awaiting its
    // ACK (16-bit sequence wraparound) is rejected, and the first keeps its retry
    // data: it is still retried on timeout. The rejection is reported only by the
    // return value: OnDeliveryFailed(id, seq) would name the first message.
    {
        NeverAckTransport transport;
        TransportMonitor monitor(std::chrono::milliseconds(20));
        RetryMonitor retry(transport, monitor, /*maxRetries=*/2);

        int failedCount = 0;
        auto conn = retry.OnDeliveryFailed.Connect(dmq::MakeDelegate(
            [&](DelegateRemoteId, uint16_t) { failedCount++; }));

        DmqHeader header(/*id=*/31, /*seqNum=*/9);
        xostringstream os1;
        os1 << "first";
        DMQ_ASSERT_TRUE(retry.SendWithRetry(os1, header) == 0);

        xostringstream os2;
        os2 << "second";
        DMQ_ASSERT_TRUE(retry.SendWithRetry(os2, header) != 0);
        DMQ_ASSERT_TRUE(transport.sendCount == 1);   // second never sent
        DMQ_ASSERT_TRUE(failedCount == 0);           // first not reported as failed

        // The first is still tracked and retried.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();
        DMQ_ASSERT_TRUE(transport.sendCount == 2);
        DMQ_ASSERT_TRUE(failedCount == 0);
    }

    ReregisterFailure(/*firstSendResult=*/0);
    ReregisterFailure(/*firstSendResult=*/-1);
}
