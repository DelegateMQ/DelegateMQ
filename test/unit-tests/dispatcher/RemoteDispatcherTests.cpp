#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include "extras/rpc/RemoteDispatcher.h"
#include "extras/util/RetryMonitor.h"
#include "extras/util/TransportMonitor.h"
#include "extras/util/ReliableTransport.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

using namespace dmq;
using namespace dmq::rpc;
using namespace dmq::transport;
using namespace dmq::util;
using namespace dmq::serialization::serializer;

namespace {

// Minimal in-process ITransport double for RemoteDispatcher's background
// RecvThread loop. Receive() must not spin the CPU at 100% while idle (the
// real transports RemoteDispatcher targets all block), so it sleeps briefly
// and returns "no data" (0, empty stream) each idle poll -- matching how a
// real blocking socket's short receive-timeout poll behaves.
class RDTestTransport : public ITransport {
public:
    std::atomic<int> sendCount{ 0 };
    dmq::DelegateRemoteId lastSendId{ 0 };
    std::string lastSendPayload;
    std::atomic<int> errorOnce{ 0 }; // next N Receive() calls report a transport error

    int Send(xostringstream& os, const DmqHeader& header) override {
        lastSendPayload = os.str();
        lastSendId = header.GetId();
        sendCount++;
        return 0;
    }

    int Receive(xstringstream& is, DmqHeader& header) override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_injectPending) {
                m_injectPending = false;
                is << m_injectPayload;
                header = DmqHeader(m_injectId, 1);
                return 0;
            }
        }
        if (errorOnce.load() > 0) {
            errorOnce--;
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return 0; // no data this poll
    }

    // Queue one message for the RecvThread's next Receive() to pick up.
    void InjectMessage(dmq::DelegateRemoteId id, const std::string& payload) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_injectId = id;
        m_injectPayload = payload;
        m_injectPending = true;
    }

private:
    std::mutex m_mutex;
    std::string m_injectPayload;
    dmq::DelegateRemoteId m_injectId{ 0 };
    bool m_injectPending = false;
};

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

// Send side for the RemoteInvokeWait tests. Never ACKs unless told to: with
// `ackOnSend` = N, the Nth Send() (counting retries) is ACKed by removing it
// from `monitor`, as a real transport does when the peer's ACK arrives. With
// `addOnSend`, Send() registers the message itself, as a raw transport with a
// TransportMonitor does when no RetryMonitor is in the stack.
class AckTestTransport : public ITransport {
public:
    std::atomic<int> sendCount{ 0 };
    int ackOnSend = 0;
    bool addOnSend = false;
    TransportMonitor* monitor = nullptr;

    int Send(xostringstream&, const DmqHeader& header) override {
        const int n = ++sendCount;
        if (addOnSend && monitor)
            monitor->Add(header.GetSeqNum(), header.GetId());
        if (monitor && n == ackOnSend)
            monitor->Remove(header.GetSeqNum(), header.GetId());
        return 0;
    }
    int Receive(xstringstream&, DmqHeader&) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return 0;
    }
};

} // namespace

// Attach() -> Start() -> Stop() (and a redundant second Stop(), plus the
// implicit one in ~RemoteDispatcher()) must all be safe. Exercises the
// off-thread marshal branches in both Start() and Stop(), RecvThread's
// startup, and the timeout timer wiring.
static void RemoteDispatcher_AttachStartStop_Lifecycle()
{
    RDTestTransport transport;
    RemoteDispatcher dispatcher;

    dispatcher.Attach(transport, transport);
    DMQ_ASSERT_TRUE(&dispatcher.GetSendTransport() == &transport);

    dispatcher.Start();
    // Let RecvThread poll at least once and the 100ms timeout timer fire at
    // least once before tearing down.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    dispatcher.Stop();
    dispatcher.Stop(); // idempotent -- must not hang or double-join

    std::cout << "RemoteDispatcher_AttachStartStop_Lifecycle() complete!" << std::endl;
}

// RegisterEndpoint(id, RemoteChannel&) + a real message arriving through
// RecvThread -> Incoming() -> the registered endpoint's Invoke(). Sender and
// receiver are separate RemoteChannel<void(int)> instances sharing the same
// test transport, exactly mirroring how two real nodes would exchange a
// message -- the sender's real Send() path produces the payload rather than
// hand-crafting wire bytes.
static void RemoteDispatcher_RegisterEndpoint_DispatchesIncoming()
{
    RDTestTransport transport;
    RemoteDispatcher dispatcher;
    dispatcher.Attach(transport, transport);

    constexpr DelegateRemoteId ID = 77;
    Serializer<void(int)> serRecv;
    Serializer<void(int)> serSend;

    std::atomic<int> received{ 0 };
    RemoteChannel<void(int)> recvChannel(transport, serRecv);
    recvChannel.Bind([&received](int v) { received = v; }, ID);
    dispatcher.RegisterEndpoint(ID, recvChannel);

    RemoteChannel<void(int)> sendChannel(transport, serSend, ID);

    dispatcher.Start();

    sendChannel(123); // real send path -> transport.Send() captures the wire payload
    DMQ_ASSERT_TRUE(transport.sendCount.load() >= 1);
    transport.InjectMessage(transport.lastSendId, transport.lastSendPayload);

    DMQ_ASSERT_TRUE(WaitFor([&] { return received.load() == 123; }, std::chrono::milliseconds(2000)));

    dispatcher.Stop();
    std::cout << "RemoteDispatcher_RegisterEndpoint_DispatchesIncoming() complete!" << std::endl;
}

// RecvThread's non-empty-error branch: a genuine transport fault (not just
// "no data yet") must surface through InternalErrorHandler -> OnError, not
// be silently swallowed or misrouted.
static void RemoteDispatcher_TransportError_FiresOnError()
{
    RDTestTransport transport;
    RemoteDispatcher dispatcher;
    dispatcher.Attach(transport, transport);

    std::atomic<int> errorCount{ 0 };
    dmq::DelegateError lastError{};
    auto conn = dispatcher.OnError.Connect(dmq::MakeDelegate(
        [&](dmq::DelegateRemoteId, dmq::DelegateError err, dmq::DelegateErrorAux) {
            // Write the payload BEFORE the atomic signal, not after: the main
            // thread's WaitFor() below only synchronizes-with writes that are
            // sequenced-before errorCount's increment on this thread. Writing
            // lastError afterward gave the reader no guarantee it would see
            // this write once errorCount.load() >= 1 became visible -- a real
            // data race TSan caught (lastError is plain, not atomic).
            lastError = err;
            errorCount++;
        }));

    transport.errorOnce = 1;
    dispatcher.Start();

    DMQ_ASSERT_TRUE(WaitFor([&] { return errorCount.load() >= 1; }, std::chrono::milliseconds(2000)));
    DMQ_ASSERT_TRUE(lastError == dmq::DelegateError::ERR_TRANSPORT_RECEIVE);

    dispatcher.Stop();
    std::cout << "RemoteDispatcher_TransportError_FiresOnError() complete!" << std::endl;
}

// AttachRetryMonitor() wires an independently-owned RetryMonitor's
// OnDeliveryFailed straight through to the dispatcher's own OnDeliveryFailed
// -- the RetryMonitor/TransportMonitor pair is deliberately NOT the
// dispatcher's internal one (that's the point of the API: the owning class
// supplies its own reliability stack). No Start()/Stop() needed; this tests
// the signal wiring in isolation, same technique as ReliableTransportTests.cpp.
static void RemoteDispatcher_AttachRetryMonitor_ForwardsDeliveryFailed()
{
    RDTestTransport transport;
    RemoteDispatcher dispatcher;
    dispatcher.Attach(transport, transport);

    TransportMonitor monitor(std::chrono::milliseconds(20));
    RetryMonitor retry(transport, monitor, /*maxRetries=*/1);
    dispatcher.AttachRetryMonitor(retry);

    std::atomic<int> failedCount{ 0 };
    auto conn = dispatcher.OnDeliveryFailed.Connect(dmq::MakeDelegate(
        [&](dmq::DelegateRemoteId, uint16_t) { failedCount++; }));

    xostringstream os;
    os << "payload";
    DmqHeader header(/*id=*/5, /*seqNum=*/9);
    retry.SendWithRetry(os, header); // never ACKed -> eventually exhausts its retry budget

    bool failed = false;
    for (int i = 0; i < 6 && !failed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        monitor.Process();
        failed = failedCount.load() >= 1;
    }
    DMQ_ASSERT_TRUE(failed);

    std::cout << "RemoteDispatcher_AttachRetryMonitor_ForwardsDeliveryFailed() complete!" << std::endl;
}

// With a RetryMonitor attached, a TIMEOUT is a retry in progress, not the
// outcome: an ACK for the retry must make RemoteInvokeWait() return true.
static void RemoteDispatcher_InvokeWait_AckedOnRetry_ReturnsTrue()
{
    AckTestTransport transport;
    RemoteDispatcher dispatcher(std::chrono::milliseconds(50));
    RetryMonitor retry(transport, dispatcher.GetTransportMonitor(), /*maxRetries=*/2);
    ReliableTransport reliable(transport, retry);
    transport.monitor = &dispatcher.GetTransportMonitor();
    transport.ackOnSend = 2;                 // first attempt lost, retry ACKed
    dispatcher.Attach(reliable, transport);
    dispatcher.AttachRetryMonitor(retry);
    dispatcher.Start();

    Serializer<void(int)> ser;
    RemoteChannel<void(int)> channel(dispatcher.GetSendTransport(), ser, 61);
    DMQ_ASSERT_TRUE(dispatcher.RemoteInvokeWait(channel, 5) == true);
    DMQ_ASSERT_TRUE(transport.sendCount.load() == 2);

    dispatcher.Stop();
    std::cout << "RemoteDispatcher_InvokeWait_AckedOnRetry_ReturnsTrue() complete!" << std::endl;
}

// Retries exhausted: RemoteInvokeWait() returns false only once the RetryMonitor
// gives up, after every attempt was sent.
static void RemoteDispatcher_InvokeWait_RetriesExhausted_ReturnsFalse()
{
    AckTestTransport transport;
    RemoteDispatcher dispatcher(std::chrono::milliseconds(50));
    RetryMonitor retry(transport, dispatcher.GetTransportMonitor(), /*maxRetries=*/1);
    ReliableTransport reliable(transport, retry);
    dispatcher.Attach(reliable, transport);
    dispatcher.AttachRetryMonitor(retry);
    dispatcher.Start();

    std::atomic<int> failedCount{ 0 };
    auto conn = dispatcher.OnDeliveryFailed.Connect(dmq::MakeDelegate(
        [&failedCount](dmq::DelegateRemoteId, uint16_t) { failedCount++; }));

    Serializer<void(int)> ser;
    RemoteChannel<void(int)> channel(dispatcher.GetSendTransport(), ser, 62);
    DMQ_ASSERT_TRUE(dispatcher.RemoteInvokeWait(channel, 5) == false);
    DMQ_ASSERT_TRUE(transport.sendCount.load() == 2);   // first attempt + 1 retry
    DMQ_ASSERT_TRUE(failedCount.load() == 1);           // ended on the final failure

    dispatcher.Stop();
    std::cout << "RemoteDispatcher_InvokeWait_RetriesExhausted_ReturnsFalse() complete!" << std::endl;
}

// No RetryMonitor: nothing will resend, so the first TIMEOUT is final and
// RemoteInvokeWait() returns false right away, not after a retry window.
static void RemoteDispatcher_InvokeWait_NoRetryMonitor_FirstTimeoutFinal()
{
    AckTestTransport transport;
    RemoteDispatcher dispatcher(std::chrono::milliseconds(50));
    transport.monitor = &dispatcher.GetTransportMonitor();
    transport.addOnSend = true;
    dispatcher.Attach(transport, transport);
    dispatcher.Start();

    Serializer<void(int)> ser;
    RemoteChannel<void(int)> channel(dispatcher.GetSendTransport(), ser, 63);
    auto start = std::chrono::steady_clock::now();
    DMQ_ASSERT_TRUE(dispatcher.RemoteInvokeWait(channel, 5) == false);
    DMQ_ASSERT_TRUE(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    DMQ_ASSERT_TRUE(transport.sendCount.load() == 1);

    dispatcher.Stop();
    std::cout << "RemoteDispatcher_InvokeWait_NoRetryMonitor_FirstTimeoutFinal() complete!" << std::endl;
}

// RemoteInvokeWaitFor(): the caller's limit ends the wait early, well before
// the retry window (4 x (200 + 100) ms + 100 ms) runs out.
static void RemoteDispatcher_InvokeWaitFor_CallerLimit()
{
    AckTestTransport transport;
    RemoteDispatcher dispatcher(std::chrono::milliseconds(200));
    RetryMonitor retry(transport, dispatcher.GetTransportMonitor(), /*maxRetries=*/3);
    ReliableTransport reliable(transport, retry);
    dispatcher.Attach(reliable, transport);
    dispatcher.AttachRetryMonitor(retry);
    dispatcher.Start();

    DMQ_ASSERT_TRUE(dispatcher.GetInvokeWaitLimit() == std::chrono::milliseconds(1300));

    Serializer<void(int)> ser;
    RemoteChannel<void(int)> channel(dispatcher.GetSendTransport(), ser, 64);
    auto start = std::chrono::steady_clock::now();
    DMQ_ASSERT_TRUE(dispatcher.RemoteInvokeWaitFor(channel, std::chrono::milliseconds(50), 5) == false);
    DMQ_ASSERT_TRUE(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500));

    dispatcher.Stop();
    std::cout << "RemoteDispatcher_InvokeWaitFor_CallerLimit() complete!" << std::endl;
}

void RemoteDispatcherTests()
{
    RemoteDispatcher_AttachStartStop_Lifecycle();
    RemoteDispatcher_RegisterEndpoint_DispatchesIncoming();
    RemoteDispatcher_TransportError_FiresOnError();
    RemoteDispatcher_AttachRetryMonitor_ForwardsDeliveryFailed();
    RemoteDispatcher_InvokeWait_AckedOnRetry_ReturnsTrue();
    RemoteDispatcher_InvokeWait_RetriesExhausted_ReturnsFalse();
    RemoteDispatcher_InvokeWait_NoRetryMonitor_FirstTimeoutFinal();
    RemoteDispatcher_InvokeWaitFor_CallerLimit();

    std::cout << "RemoteDispatcherTests() complete!" << std::endl;
}
