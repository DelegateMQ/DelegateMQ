#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include "extras/util/RetryMonitor.h"
#include "extras/util/TransportMonitor.h"
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
}
