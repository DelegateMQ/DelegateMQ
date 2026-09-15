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
}
