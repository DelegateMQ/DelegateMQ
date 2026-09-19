#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include "extras/util/ReliableTransport.h"
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
    // TransportMonitor -- same technique as RetryMonitorTest.cpp's
    // NeverAckTransport, reused here to prove ReliableTransport::Send() drives
    // the exact same retry-exhaustion path as calling RetryMonitor directly.
    class NeverAckTransport : public ITransport {
    public:
        int sendCount = 0;
        int Send(xostringstream&, const DmqHeader&) override {
            sendCount++;
            return 0;
        }
        int Receive(xstringstream&, DmqHeader&) override { return -1; }
    };

    // Transport whose Receive() returns known, distinguishable data so the
    // pass-through path can be verified byte-for-byte, not just by return code.
    class RecordingTransport : public ITransport {
    public:
        int sendCount = 0;
        int receiveCount = 0;
        int receiveReturn = 0;
        dmq::xstring receivePayload;
        dmq::DelegateRemoteId receiveId = 0;
        uint16_t receiveSeq = 0;

        int Send(xostringstream&, const DmqHeader&) override {
            sendCount++;
            return 0;
        }
        int Receive(xstringstream& is, DmqHeader& header) override {
            receiveCount++;
            is << receivePayload;
            header = DmqHeader(receiveId, receiveSeq);
            return receiveReturn;
        }
    };
}

void ReliableTransportTests()
{
    // 1. Send() forwards to RetryMonitor::SendWithRetry() -- a message that IS
    //    acknowledged before timing out never retries and never reports failure,
    //    identical to calling RetryMonitor::SendWithRetry() directly.
    {
        NeverAckTransport transport;
        TransportMonitor monitor(std::chrono::milliseconds(20));
        RetryMonitor retry(transport, monitor, /*maxRetries=*/2);
        ReliableTransport reliable(transport, retry);

        int failedCount = 0;
        auto conn = retry.OnDeliveryFailed.Connect(dmq::MakeDelegate(
            [&](DelegateRemoteId, uint16_t) { failedCount++; }));

        xostringstream os;
        os << "payload";
        DmqHeader header(/*id=*/99, /*seqNum=*/3);

        DMQ_ASSERT_TRUE(reliable.Send(os, header) == 0);
        DMQ_ASSERT_TRUE(transport.sendCount == 1);
        monitor.Remove(header.GetSeqNum(), header.GetId()); // ACK arrives

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();
        DMQ_ASSERT_TRUE(transport.sendCount == 1); // no retry
        DMQ_ASSERT_TRUE(failedCount == 0);
    }

    // 2. Send() drives the exact same retry-exhaustion path as
    //    RetryMonitor::SendWithRetry() called directly: OnDeliveryFailed fires
    //    exactly once, only after the retry budget is exhausted.
    {
        NeverAckTransport transport;
        TransportMonitor monitor(std::chrono::milliseconds(20));
        RetryMonitor retry(transport, monitor, /*maxRetries=*/2);
        ReliableTransport reliable(transport, retry);

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

        int err = reliable.Send(os, header);
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
    }

    // 3. Receive() pass-through: ReliableTransport::Receive() forwards
    //    directly to the underlying physical transport, unmodified -- no
    //    interception, no extra call, exact data and return code preserved.
    //    (RetryMonitor doesn't implement Receive() at all, so this path is
    //    exercised only by ReliableTransport's own adapter logic.)
    {
        RecordingTransport transport;
        transport.receiveReturn = 0;
        transport.receivePayload = "hello";
        transport.receiveId = 55;
        transport.receiveSeq = 11;

        TransportMonitor monitor(std::chrono::milliseconds(20));
        RetryMonitor retry(transport, monitor, /*maxRetries=*/2);
        ReliableTransport reliable(transport, retry);

        xstringstream is;
        DmqHeader header;
        int err = reliable.Receive(is, header);

        DMQ_ASSERT_TRUE(err == 0);
        DMQ_ASSERT_TRUE(transport.receiveCount == 1);
        DMQ_ASSERT_TRUE(header.GetId() == 55);
        DMQ_ASSERT_TRUE(header.GetSeqNum() == 11);
        DMQ_ASSERT_TRUE(is.str() == "hello");

        // A non-zero return from the physical transport (e.g. "nothing to
        // receive") must also pass through unchanged, not be swallowed or
        // remapped by the adapter.
        transport.receiveReturn = -1;
        xstringstream is2;
        DmqHeader header2;
        DMQ_ASSERT_TRUE(reliable.Receive(is2, header2) == -1);
        DMQ_ASSERT_TRUE(transport.receiveCount == 2);
    }

    // 4. Default-construct + Init() is equivalent to the two-arg constructor.
    {
        NeverAckTransport transport;
        TransportMonitor monitor(std::chrono::milliseconds(20));
        RetryMonitor retry(transport, monitor, /*maxRetries=*/1);

        ReliableTransport reliable; // default-constructed, not yet wired
        reliable.Init(transport, retry);

        xostringstream os;
        os << "payload";
        DmqHeader header(/*id=*/1, /*seqNum=*/1);
        DMQ_ASSERT_TRUE(reliable.Send(os, header) == 0);
        DMQ_ASSERT_TRUE(transport.sendCount == 1);
    }
}
