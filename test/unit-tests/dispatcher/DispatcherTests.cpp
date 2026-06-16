#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include "extras/dispatcher/Dispatcher.h"
#include "extras/dispatcher/RemoteChannel.h"
#include <iostream>

using namespace std;
using namespace dmq;
using namespace dmq::transport;
using namespace dmq::serialization::serializer;

namespace {
    class MockTransport : public ITransport {
    public:
        int lastId = -1;
        uint16_t lastSeq = 0;
        size_t lastPayloadSize = 0;
        int sendCount = 0;

        int Send(xostringstream& os, const DmqHeader& header) override {
            lastId = header.GetId();
            lastSeq = header.GetSeqNum();
            lastPayloadSize = os.str().size();
            sendCount++;
            return 0;
        }
        int Receive(xstringstream& is, DmqHeader& header) override { return 0; }
    };

    void FreeFunc(int i) {}
}

void DispatcherTests()
{
    // Test Dispatcher class directly
    {
        MockTransport transport;
        Dispatcher dispatcher;
        dispatcher.SetTransport(&transport);

        xostringstream os;
        os << "test data";
        DelegateRemoteId id(100);

        uint16_t expectedSeq = 0;
        int err = dispatcher.Dispatch(os, id);
        
        ASSERT_TRUE(err == 0);
        ASSERT_TRUE(transport.sendCount == 1);
        ASSERT_TRUE(transport.lastId == 100);
        ASSERT_TRUE(transport.lastSeq == expectedSeq);
    }

    // Test RemoteChannel class
    {
        MockTransport transport;
        dmq::serialization::serializer::Serializer<void(int)> serializer;
        RemoteChannel<void(int)> channel(transport, serializer);

        ASSERT_TRUE(channel.GetDispatcher() != nullptr);
        ASSERT_TRUE(channel.GetSerializer() == &serializer);

        // Bind a lambda as receiver
        int receivedVal = 0;
        channel.Bind([&receivedVal](int i) { receivedVal = i; }, DelegateRemoteId(200));

        ASSERT_TRUE(channel.GetRemoteId() == 200);

        // Invoke sender side
        channel(123);

        ASSERT_TRUE(transport.sendCount == 1);
        ASSERT_TRUE(transport.lastId == 200);
        
        // Test manual Receive (mirroring transport reception)
        xstringstream is;
        // The transport would normally fill 'is'. Since we're testing the channel's 
        // internal invoker, we can call Invoke() directly if we had the serialized data.
        // For this unit test, verifying the binding and dispatch is sufficient as 
        // DelegateFunctionRemote is tested elsewhere.
    }

    // Test MakeDelegate overloads with RemoteChannel
    {
        MockTransport transport;
        dmq::serialization::serializer::Serializer<void(int)> serializer;
        RemoteChannel<void(int)> channel(transport, serializer);

        auto d = MakeDelegate(&FreeFunc, DelegateRemoteId(300), channel);
        
        d(456);
        ASSERT_TRUE(transport.sendCount == 1);
        ASSERT_TRUE(transport.lastId == 300);
    }
}
