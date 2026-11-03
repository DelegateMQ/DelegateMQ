#include "DelegateMQ.h"
#include <iostream>
#include <queue>
#include <atomic>
#include <thread>
#include <chrono>

#if defined(DMQ_DATABUS)

using namespace dmq;
using namespace dmq::os;
using namespace dmq::transport;
using namespace dmq::databus;
using namespace dmq::serialization::serializer;

// Simple loopback transport for testing
class DataBusLoopbackTransport : public ITransport {
public:
    struct Packet {
        DmqHeader header;
        std::vector<char> data;
    };

    virtual int Send(xostringstream& os, const DmqHeader& header) override {
        xstring s = os.str();
        std::vector<char> data(s.begin(), s.end());
        m_queue.push({header, data});
        return 0;
    }

    virtual int Receive(xstringstream& is, DmqHeader& header) override {
        if (m_queue.empty()) return -1;
        Packet p = m_queue.front();
        m_queue.pop();
        header = p.header;
        is.write(p.data.data(), p.data.size());
        return 0;
    }

    /// Queue a message as if received from another node.
    void Inject(const DmqHeader& header, const xstring& payload) {
        m_queue.push({header, std::vector<char>(payload.begin(), payload.end())});
    }

private:
    std::queue<Packet> m_queue;
};

int DataBusRemoteTestMain() {
    std::cout << "Starting DataBusRemoteTest..." << std::endl;

    // 1. Basic remote publish/subscribe via loopback transport
    {
        DataBus::ResetForTesting();
        DataBusLoopbackTransport transport;
        dmq::serialization::serializer::Serializer<void(int)> serializer;

        auto nodeB = std::make_shared<Participant>(transport);
        int receivedValue = 0;
        nodeB->RegisterHandler<int>(100, serializer, [&](int val) {
            receivedValue = val;
        });

        auto nodeA_participant = std::make_shared<Participant>(transport);
        nodeA_participant->AddRemoteTopic("remote/topic", 100);
        DataBus::AddParticipant(nodeA_participant);
        DataBus::RegisterSerializer<int>("remote/topic", serializer);

        DataBus::Publish<int>("remote/topic", 42);
        nodeB->ProcessIncoming();

        DMQ_ASSERT_TRUE(receivedValue == 42);
    }

    // 2. PublishLocal vs Publish — PublishLocal skips remote participants
    {
        DataBus::ResetForTesting();
        dmq::serialization::serializer::Serializer<void(int)> serializer;

        // Count how many times the transport is asked to send
        struct CountingTransport : public ITransport {
            int sendCount = 0;
            int Send(xostringstream&, const DmqHeader&) override { sendCount++; return 0; }
            int Receive(xstringstream&, DmqHeader&) override { return -1; }
        } countingTransport;

        auto participant = std::make_shared<Participant>(countingTransport);
        participant->AddRemoteTopic("local/topic", 200);
        DataBus::AddParticipant(participant);
        DataBus::RegisterSerializer<int>("local/topic", serializer);

        bool localReceived = false;
        auto conn = DataBus::Subscribe<int>("local/topic", [&](int) { localReceived = true; });

        // PublishLocal: local subscriber gets it; transport is NOT called
        DataBus::PublishLocal<int>("local/topic", 1);
        DMQ_ASSERT_TRUE(localReceived == true);
        DMQ_ASSERT_TRUE(countingTransport.sendCount == 0);

        // Publish: local subscriber gets it AND transport is called
        localReceived = false;
        DataBus::Publish<int>("local/topic", 2);
        DMQ_ASSERT_TRUE(localReceived == true);
        DMQ_ASSERT_TRUE(countingTransport.sendCount == 1);
    }

    // 3. AddIncomingTopic vs AddRelayTopic
    //    AddIncomingTopic → handler calls PublishLocal → does NOT re-forward to remote
    //    AddRelayTopic    → handler calls Publish     → DOES re-forward to remote
    {
        dmq::serialization::serializer::Serializer<void(int)> serializer;

        // Shared helper: inject one serialized int packet into transport via a temporary source node
        auto injectPacket = [&](DataBusLoopbackTransport& transport, dmq::DelegateRemoteId remoteId, int value) {
            Participant sourceNode(transport);
            sourceNode.AddRemoteTopic("src/inject", remoteId);
            sourceNode.Send("src/inject", value, serializer);
        };

        // 3a. AddIncomingTopic → PublishLocal → no re-forward
        {
            DataBus::ResetForTesting();

            struct CountingTransport : public ITransport {
                int sendCount = 0;
                int Send(xostringstream&, const DmqHeader&) override { sendCount++; return 0; }
                int Receive(xstringstream&, DmqHeader&) override { return -1; }
            } downstreamTransport;

            // Register a downstream participant that would receive re-forwarded data
            auto downParticipant = std::make_shared<Participant>(downstreamTransport);
            downParticipant->AddRemoteTopic("relay/data", 300);
            DataBus::AddParticipant(downParticipant);
            DataBus::RegisterSerializer<int>("relay/data", serializer);

            int localValue = -1;
            auto conn = DataBus::Subscribe<int>("relay/data", [&](int v) { localValue = v; });

            DataBusLoopbackTransport upstreamTransport;
            auto incomingParticipant = std::make_shared<Participant>(upstreamTransport);
            DataBus::AddIncomingTopic<int>("relay/data", 400, *incomingParticipant, serializer);

            injectPacket(upstreamTransport, 400, 55);
            incomingParticipant->ProcessIncoming(); // → PublishLocal("relay/data", 55)

            DMQ_ASSERT_TRUE(localValue == 55);                  // local subscriber got it
            DMQ_ASSERT_TRUE(downstreamTransport.sendCount == 0); // NOT re-forwarded
        }

        // 3b. AddRelayTopic → Publish → re-forwards to registered remote participants
        {
            DataBus::ResetForTesting();

            struct CountingTransport : public ITransport {
                int sendCount = 0;
                int Send(xostringstream&, const DmqHeader&) override { sendCount++; return 0; }
                int Receive(xstringstream&, DmqHeader&) override { return -1; }
            } downstreamTransport;

            auto downParticipant = std::make_shared<Participant>(downstreamTransport);
            downParticipant->AddRemoteTopic("relay/data", 300);
            DataBus::AddParticipant(downParticipant);
            DataBus::RegisterSerializer<int>("relay/data", serializer);

            int localValue = -1;
            auto conn = DataBus::Subscribe<int>("relay/data", [&](int v) { localValue = v; });

            DataBusLoopbackTransport upstreamTransport;
            auto relayParticipant = std::make_shared<Participant>(upstreamTransport);
            DataBus::AddRelayTopic<int>("relay/data", 400, *relayParticipant, serializer);

            injectPacket(upstreamTransport, 400, 77);
            relayParticipant->ProcessIncoming(); // → Publish("relay/data", 77)

            DMQ_ASSERT_TRUE(localValue == 77);                   // local subscriber got it
            DMQ_ASSERT_TRUE(downstreamTransport.sendCount > 0);  // WAS re-forwarded
        }

        // 3c. AddRelayTopic on a duplex link — the inbound participant is also registered
        //     with AddParticipant (it sends other topics on the same link). Relayed data must
        //     go downstream only, never echo back out the link it arrived on.
        {
            DataBus::ResetForTesting();

            struct CountingTransport : public ITransport {
                int sendCount = 0;
                int Send(xostringstream&, const DmqHeader&) override { sendCount++; return 0; }
                int Receive(xstringstream&, DmqHeader&) override { return -1; }
            } downstreamTransport;

            // Loopback that also counts sends made after the test packet is injected
            struct CountingLoopbackTransport : public DataBusLoopbackTransport {
                int sendCount = 0;
                int Send(xostringstream& os, const DmqHeader& header) override {
                    sendCount++;
                    return DataBusLoopbackTransport::Send(os, header);
                }
            } upstreamTransport;

            auto downParticipant = std::make_shared<Participant>(downstreamTransport);
            downParticipant->AddRemoteTopic("relay/data", 300);
            DataBus::AddParticipant(downParticipant);
            DataBus::RegisterSerializer<int>("relay/data", serializer);

            auto relayParticipant = std::make_shared<Participant>(upstreamTransport);
            DataBus::AddParticipant(relayParticipant);  // duplex: same link is also an outgoing participant
            DataBus::AddRelayTopic<int>("relay/data", 400, *relayParticipant, serializer);

            injectPacket(upstreamTransport, 400, 88);
            upstreamTransport.sendCount = 0;           // ignore the injection send itself
            relayParticipant->ProcessIncoming();       // → Publish("relay/data", 88)

            DMQ_ASSERT_TRUE(downstreamTransport.sendCount > 0);  // forwarded downstream
            DMQ_ASSERT_TRUE(upstreamTransport.sendCount == 0);   // NOT echoed back upstream
        }
    }

    // 4. Participant::SetSendThread — transport.Send executes on the send thread, not caller
    {
        DataBus::ResetForTesting();
        dmq::serialization::serializer::Serializer<void(int)> serializer;

        dmq::os::Thread sendWorker("RemoteSendWorker");
        sendWorker.CreateThread();

        std::atomic<bool> sendCalledOnWorker{false};

        class TrackingTransport : public ITransport {
        public:
            dmq::os::Thread* expectedThread = nullptr;
            std::atomic<bool>* calledOnWorker = nullptr;
            int Send(xostringstream&, const DmqHeader&) override {
                if (calledOnWorker)
                    calledOnWorker->store(expectedThread->IsCurrentThread());
                return 0;
            }
            int Receive(xstringstream&, DmqHeader&) override { return -1; }
        } trackingTransport;

        trackingTransport.expectedThread = &sendWorker;
        trackingTransport.calledOnWorker = &sendCalledOnWorker;

        auto participant = std::make_shared<Participant>(trackingTransport);
        participant->AddRemoteTopic("send/topic", 500);
        participant->SetSendThread(&sendWorker);
        DataBus::AddParticipant(participant);
        DataBus::RegisterSerializer<int>("send/topic", serializer);

        DataBus::Publish<int>("send/topic", 42); // returns immediately; send is async

        int retries = 0;
        while (!sendCalledOnWorker && retries++ < 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        DMQ_ASSERT_TRUE(sendCalledOnWorker == true);

        sendWorker.ExitThread();
    }

    // 5. DmqHeader wire format: 10 bytes, big-endian, node ID and version.
    {
        DmqHeader h(0x1234, 0x5678, 0x9ABC);
        h.SetNodeId(7);
        uint8_t buf[DmqHeader::HEADER_SIZE] = {};
        h.Encode(buf);
        const uint8_t expected[DmqHeader::HEADER_SIZE] =
            { 0xAA, 0x55, 7, DmqHeader::VERSION, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC };
        for (size_t i = 0; i < DmqHeader::HEADER_SIZE; ++i)
            DMQ_ASSERT_TRUE(buf[i] == expected[i]);

        DmqHeader d;
        d.Decode(buf);
        DMQ_ASSERT_TRUE(d.IsValid());
        DMQ_ASSERT_TRUE(d.GetNodeId() == 7);
        DMQ_ASSERT_TRUE(d.GetId() == 0x1234);
        DMQ_ASSERT_TRUE(d.GetSeqNum() == 0x5678);
        DMQ_ASSERT_TRUE(d.GetLength() == 0x9ABC);

        buf[3] = DmqHeader::VERSION + 1;   // well-formed frame, unknown version
        d.Decode(buf);
        DMQ_ASSERT_TRUE(d.HasValidMarker());
        DMQ_ASSERT_TRUE(!d.IsValid());

        // New headers carry this process's node ID.
        const uint8_t saved = DmqHeader::GetLocalNodeId();
        DmqHeader::SetLocalNodeId(42);
        DMQ_ASSERT_TRUE(DmqHeader(1, 2).GetNodeId() == 42);
        DmqHeader::SetLocalNodeId(saved);
    }

    // 6. Duplicate filtering is per sending node. Two nodes may use the same
    //    sequence number for the same remote ID; only a repeat from the same node
    //    (a retry) is a duplicate.
    {
        DataBus::ResetForTesting();
        DataBusLoopbackTransport transport;
        dmq::serialization::serializer::Serializer<void(int)> serializer;

        auto receiver = std::make_shared<Participant>(transport);
        int count = 0;
        int last = 0;
        receiver->RegisterHandler<int>(300, serializer, [&](int val) {
            count++;
            last = val;
        });

        auto inject = [&](uint8_t node, uint16_t seq, int value) {
            xostringstream os(std::ios::in | std::ios::out | std::ios::binary);
            serializer.Write(os, value);
            DmqHeader h(300, seq);
            h.SetNodeId(node);
            transport.Inject(h, os.str());
            receiver->ProcessIncoming();
        };

        inject(1, 5, 10);
        DMQ_ASSERT_TRUE(count == 1 && last == 10);

        inject(2, 5, 20);                  // other node, same seq: delivered
        DMQ_ASSERT_TRUE(count == 2 && last == 20);

        inject(1, 5, 10);                  // same node, same seq: retry, dropped
        DMQ_ASSERT_TRUE(count == 2);

        inject(1, 6, 11);                  // same node, next seq: delivered
        DMQ_ASSERT_TRUE(count == 3 && last == 11);

        // A header with an unknown version is rejected, not delivered.
        {
            xostringstream os(std::ios::in | std::ios::out | std::ios::binary);
            serializer.Write(os, 99);
            DmqHeader h(300, 7);
            h.SetNodeId(1);
            h.SetVersion(DmqHeader::VERSION + 1);
            transport.Inject(h, os.str());
            receiver->ProcessIncoming();
            DMQ_ASSERT_TRUE(count == 3);
        }

        // History expires: once the retry window has passed, the same number
        // from the same node is a restarted sender, not a retry.
        receiver->SetSeqHistoryExpiry(std::chrono::milliseconds(50));
        inject(3, 9, 30);
        DMQ_ASSERT_TRUE(count == 4);
        inject(3, 9, 30);                  // within the window: dropped
        DMQ_ASSERT_TRUE(count == 4);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        inject(3, 9, 31);                  // after the window: delivered
        DMQ_ASSERT_TRUE(count == 5 && last == 31);
    }

    std::cout << "DataBusRemoteTest PASSED!" << std::endl;
    return 0;
}

#else

int DataBusRemoteTestMain() {
    return 0;
}

#endif
