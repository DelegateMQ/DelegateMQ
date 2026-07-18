#include "DelegateMQ.h"
#include "extras/dispatcher/RemoteChannel.h"
#include "UnitTestCommon.h"
#include <iostream>
#include <sstream>

using namespace dmq;
using namespace dmq::transport;
using namespace std;
using namespace UnitTestData;

// RemoteChannelTests.cpp tests the RemoteChannel aggregator class and its MakeDelegate
// overloads. Tests follow the same pattern as DelegateRemoteTests:
//   1. Call sender delegate  -> serializes args into channel's stream, dispatches via
//      Dispatcher -> MockTransport::Send captures the payload.
//   2. Seek payload stream to 0, call receiver.Invoke() -> deserializes and calls target.

namespace RemoteChannelTest
{
    const DelegateRemoteId REMOTE_ID = 42;

    // ---- target functions -------------------------------------------------------

    static bool g_invoked = false;
    static int  g_lastInt = 0;

    struct RemoteData {
        RemoteData() = default;
        RemoteData(int x, int y) : x(x), y(y) {}
        int x = 0;
        int y = 0;
    };

    std::ostream& operator<<(std::ostream& os, const RemoteData& d) {
        os << d.x << "\n" << d.y << "\n";
        return os;
    }
    std::istream& operator>>(std::istream& is, RemoteData& d) {
        is >> d.x >> d.y;
        return is;
    }

    static void FreeFuncInt(int i) {
        g_invoked = true;
        g_lastInt = i;
        ASSERT_TRUE(i == TEST_INT);
    }

    static void FreeFuncIntRef(int& i) {
        g_invoked = true;
        g_lastInt = i;
        ASSERT_TRUE(i == TEST_INT);
    }

    static void FreeFuncIntPtr(int* pi) {
        g_invoked = true;
        ASSERT_TRUE(pi != nullptr);
        ASSERT_TRUE(*pi == TEST_INT);
        g_lastInt = *pi;
    }

    static void FreeFuncRemoteData(RemoteData& d) {
        g_invoked = true;
        ASSERT_TRUE(d.x == TEST_INT);
        ASSERT_TRUE(d.y == TEST_INT + 1);
    }

    static void FreeFuncTwoArgs(int a, int b) {
        g_invoked = true;
        ASSERT_TRUE(a == TEST_INT);
        ASSERT_TRUE(b == TEST_INT + 1);
    }

    class RemoteClass {
    public:
        void MemberFuncInt(int i) {
            g_invoked = true;
            g_lastInt = i;
            ASSERT_TRUE(i == TEST_INT);
        }
        void MemberFuncIntConst(int i) const {
            g_invoked = true;
            ASSERT_TRUE(i == TEST_INT);
        }
        void MemberFuncRemoteData(RemoteData& d) {
            g_invoked = true;
            ASSERT_TRUE(d.x == TEST_INT);
            ASSERT_TRUE(d.y == TEST_INT + 1);
        }
    };

    // ---- stream helpers ---------------------------------------------------------

    template<typename... Ts>
    void serialize_args(std::ostream&) {}

    template<typename A, typename... Rest>
    void serialize_args(std::ostream& os, A& a, Rest... rest) {
        os << a << "\n";
        serialize_args(os, rest...);
    }
    template<typename A, typename... Rest>
    void serialize_args(std::ostream& os, A* a, Rest... rest) {
        os << *a << "\n";
        serialize_args(os, rest...);
    }

    template<typename... Ts>
    void deserialize_args(std::istream&) {}

    template<typename A, typename... Rest>
    void deserialize_args(std::istream& is, A& a, Rest&&... rest) {
        is >> a;
        deserialize_args(is, rest...);
    }
    template<typename A, typename... Rest>
    void deserialize_args(std::istream& is, A* a, Rest&&... rest) {
        is >> *a;
        deserialize_args(is, rest...);
    }

    // ---- test serializer (named RCSerializer to avoid clash with predefined Serializer) ---

    template <class R>
    struct RCSerializer; // Not defined

    template<class RetType, class... Args>
    class RCSerializer<RetType(Args...)> : public ISerializer<RetType(Args...)> {
    public:
        std::ostream& Write(std::ostream& os, const Args&... args) override {
            serialize_args(os, args...);
            return os;
        }
        std::istream& Read(std::istream& is, Args&... args) override {
            deserialize_args(is, args...);
            return is;
        }
    };

    // ---- mock transport --------------------------------------------------------

    /// Captures the serialized payload written by Dispatcher::Dispatch so
    /// tests can feed it into a receiver delegate's Invoke().
    class MockTransport : public ITransport {
    public:
        int Send(xostringstream& os, const DmqHeader& header) override {
            m_payload = os.str();
            m_lastId  = header.GetId();
            m_sendCount++;
            return m_sendReturnValue;
        }

        int Receive(xstringstream&, DmqHeader&) override { return 0; }

        /// Returns a fresh stringstream containing the last captured payload,
        /// positioned at the start for reading.
        std::stringstream GetPayloadStream() const {
            return std::stringstream(m_payload, std::ios::in | std::ios::out | std::ios::binary);
        }

        std::string    m_payload;
        uint16_t       m_lastId          = 0;
        int            m_sendCount       = 0;
        int            m_sendReturnValue = 0;
    };

} // namespace RemoteChannelTest

using namespace RemoteChannelTest;

// ---- Construction / accessors --------------------------------------------------

static void RemoteChannel_Construction()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;

    RemoteChannel<void(int)> channel(transport, serializer);

    ASSERT_TRUE(channel.GetDispatcher() != nullptr);
    ASSERT_TRUE(channel.GetSerializer() == &serializer);
    ASSERT_TRUE(channel.GetStream().good());
}

// ---- MakeDelegate creates a configured sender delegate -------------------------

static void RemoteChannel_MakeDelegate_FreeFunc()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    auto d = MakeDelegate(&FreeFuncInt, REMOTE_ID, channel);

    ASSERT_TRUE(!d.Empty());
    ASSERT_TRUE(d.GetRemoteId() == REMOTE_ID);
    d(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);
    ASSERT_TRUE(transport.m_lastId == REMOTE_ID);
}

static void RemoteChannel_MakeDelegate_MemberFunc()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    RemoteClass obj;
    auto d = MakeDelegate(&obj, &RemoteClass::MemberFuncInt, REMOTE_ID, channel);

    ASSERT_TRUE(!d.Empty());
    ASSERT_TRUE(d.GetRemoteId() == REMOTE_ID);
    d(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);
}

static void RemoteChannel_MakeDelegate_ConstMemberFunc()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    RemoteClass obj;
    auto d = MakeDelegate(&obj, &RemoteClass::MemberFuncIntConst, REMOTE_ID, channel);

    ASSERT_TRUE(!d.Empty());
    d(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);
}

static void RemoteChannel_MakeDelegate_SharedPtrMemberFunc()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    auto obj = std::make_shared<RemoteClass>();
    auto d = MakeDelegate(obj, &RemoteClass::MemberFuncInt, REMOTE_ID, channel);

    ASSERT_TRUE(!d.Empty());
    d(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);
}

static void RemoteChannel_MakeDelegate_StdFunction()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    std::function<void(int)> func = [](int i) {
        ASSERT_TRUE(i == TEST_INT);
    };
    auto d = MakeDelegate(func, REMOTE_ID, channel);

    ASSERT_TRUE(!d.Empty());
    d(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);
}

// ---- Full round-trip tests: sender dispatches, receiver invokes ----------------

static void RemoteChannel_RoundTrip_FreeFunc_Int()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    auto sender = MakeDelegate(&FreeFuncInt, REMOTE_ID, channel);

    g_invoked = false;
    g_lastInt = 0;
    sender(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);

    DelegateFreeRemote<void(int)> receiver(&FreeFuncInt, REMOTE_ID);
    receiver.SetSerializer(&serializer);

    auto recvStream = transport.GetPayloadStream();
    recvStream.seekg(0);
    receiver.Invoke(recvStream);

    ASSERT_TRUE(g_invoked);
    ASSERT_TRUE(g_lastInt == TEST_INT);
}

static void RemoteChannel_RoundTrip_FreeFunc_IntRef()
{
    MockTransport transport;
    RCSerializer<void(int&)> serializer;
    RemoteChannel<void(int&)> channel(transport, serializer);

    auto sender = MakeDelegate(&FreeFuncIntRef, REMOTE_ID, channel);

    g_invoked = false;
    int val = TEST_INT;
    sender(val);
    ASSERT_TRUE(transport.m_sendCount == 1);

    DelegateFreeRemote<void(int&)> receiver(&FreeFuncIntRef, REMOTE_ID);
    receiver.SetSerializer(&serializer);

    auto recvStream = transport.GetPayloadStream();
    recvStream.seekg(0);
    receiver.Invoke(recvStream);

    ASSERT_TRUE(g_invoked);
}

static void RemoteChannel_RoundTrip_FreeFunc_IntPtr()
{
    MockTransport transport;
    RCSerializer<void(int*)> serializer;
    RemoteChannel<void(int*)> channel(transport, serializer);

    auto sender = MakeDelegate(&FreeFuncIntPtr, REMOTE_ID, channel);

    g_invoked = false;
    int val = TEST_INT;
    sender(&val);
    ASSERT_TRUE(transport.m_sendCount == 1);

    DelegateFreeRemote<void(int*)> receiver(&FreeFuncIntPtr, REMOTE_ID);
    receiver.SetSerializer(&serializer);

    auto recvStream = transport.GetPayloadStream();
    recvStream.seekg(0);
    receiver.Invoke(recvStream);

    ASSERT_TRUE(g_invoked);
}

static void RemoteChannel_RoundTrip_FreeFunc_RemoteData()
{
    MockTransport transport;
    RCSerializer<void(RemoteData&)> serializer;
    RemoteChannel<void(RemoteData&)> channel(transport, serializer);

    auto sender = MakeDelegate(&FreeFuncRemoteData, REMOTE_ID, channel);

    g_invoked = false;
    RemoteData d(TEST_INT, TEST_INT + 1);
    sender(d);
    ASSERT_TRUE(transport.m_sendCount == 1);

    DelegateFreeRemote<void(RemoteData&)> receiver(&FreeFuncRemoteData, REMOTE_ID);
    receiver.SetSerializer(&serializer);

    auto recvStream = transport.GetPayloadStream();
    recvStream.seekg(0);
    receiver.Invoke(recvStream);

    ASSERT_TRUE(g_invoked);
}

static void RemoteChannel_RoundTrip_FreeFunc_TwoArgs()
{
    MockTransport transport;
    RCSerializer<void(int, int)> serializer;
    RemoteChannel<void(int, int)> channel(transport, serializer);

    auto sender = MakeDelegate(&FreeFuncTwoArgs, REMOTE_ID, channel);

    g_invoked = false;
    sender(TEST_INT, TEST_INT + 1);
    ASSERT_TRUE(transport.m_sendCount == 1);

    DelegateFreeRemote<void(int, int)> receiver(&FreeFuncTwoArgs, REMOTE_ID);
    receiver.SetSerializer(&serializer);

    auto recvStream = transport.GetPayloadStream();
    recvStream.seekg(0);
    receiver.Invoke(recvStream);

    ASSERT_TRUE(g_invoked);
}

static void RemoteChannel_RoundTrip_MemberFunc()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    RemoteClass obj;
    auto sender = MakeDelegate(&obj, &RemoteClass::MemberFuncInt, REMOTE_ID, channel);

    g_invoked = false;
    sender(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);

    DelegateMemberRemote<RemoteClass, void(int)> receiver(&obj, &RemoteClass::MemberFuncInt, REMOTE_ID);
    receiver.SetSerializer(&serializer);

    auto recvStream = transport.GetPayloadStream();
    recvStream.seekg(0);
    receiver.Invoke(recvStream);

    ASSERT_TRUE(g_invoked);
}

static void RemoteChannel_RoundTrip_MemberFunc_RemoteData()
{
    MockTransport transport;
    RCSerializer<void(RemoteData&)> serializer;
    RemoteChannel<void(RemoteData&)> channel(transport, serializer);

    RemoteClass obj;
    auto sender = MakeDelegate(&obj, &RemoteClass::MemberFuncRemoteData, REMOTE_ID, channel);

    g_invoked = false;
    RemoteData d(TEST_INT, TEST_INT + 1);
    sender(d);
    ASSERT_TRUE(transport.m_sendCount == 1);

    DelegateMemberRemote<RemoteClass, void(RemoteData&)> receiver(&obj, &RemoteClass::MemberFuncRemoteData, REMOTE_ID);
    receiver.SetSerializer(&serializer);

    auto recvStream = transport.GetPayloadStream();
    recvStream.seekg(0);
    receiver.Invoke(recvStream);

    ASSERT_TRUE(g_invoked);
}

static void RemoteChannel_RoundTrip_SharedPtrMemberFunc()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    auto obj = std::make_shared<RemoteClass>();
    auto sender = MakeDelegate(obj, &RemoteClass::MemberFuncInt, REMOTE_ID, channel);

    g_invoked = false;
    sender(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);

    DelegateMemberRemote<RemoteClass, void(int)> receiver(obj, &RemoteClass::MemberFuncInt, REMOTE_ID);
    receiver.SetSerializer(&serializer);

    auto recvStream = transport.GetPayloadStream();
    recvStream.seekg(0);
    receiver.Invoke(recvStream);

    ASSERT_TRUE(g_invoked);
}

// ---- Error path: transport Send failure propagates to error handler ------------

static void RemoteChannel_DispatchError_PropagatedToErrorHandler()
{
    MockTransport transport;
    transport.m_sendReturnValue = -1;  // Simulate send failure

    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    auto sender = MakeDelegate(&FreeFuncInt, REMOTE_ID, channel);

    DelegateError lastError = DelegateError::SUCCESS;
    std::function<void(DelegateRemoteId, DelegateError, DelegateErrorAux)> errCb =
        [&](DelegateRemoteId, DelegateError err, DelegateErrorAux) { lastError = err; };
    sender.SetErrorHandler(MakeDelegate(errCb));

    sender(TEST_INT);

    ASSERT_TRUE(transport.m_sendCount == 1);
    ASSERT_TRUE(lastError == DelegateError::ERR_DISPATCH);
}

// ---- MakeDelegate result matches manual SetXxx wiring -------------------------

static void RemoteChannel_MakeDelegate_MatchesManualWiring()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    // MakeDelegate path
    auto channelDelegate = MakeDelegate(&FreeFuncInt, REMOTE_ID, channel);

    // Manual wiring path (old API) using the channel's own dispatcher
    xostringstream manualStream(ios::in | ios::out | ios::binary);
    DelegateFreeRemote<void(int)> manualDelegate(FreeFuncInt, REMOTE_ID);
    manualDelegate.SetDispatcher(channel.GetDispatcher());
    manualDelegate.SetSerializer(&serializer);
    manualDelegate.SetStream(&manualStream);

    // Both should route through the same dispatcher -> same transport
    channelDelegate(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);
    ASSERT_TRUE(transport.m_lastId == REMOTE_ID);

    manualDelegate(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 2);
    ASSERT_TRUE(transport.m_lastId == REMOTE_ID);
}

// ---- Raw lambda MakeDelegate ---------------------------------------------------

static void RemoteChannel_MakeDelegate_RawLambda()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    // Inline raw lambda — no std::function wrapper
    auto d1 = MakeDelegate([](int i) { ASSERT_TRUE(i == TEST_INT); }, REMOTE_ID, channel);
    ASSERT_TRUE(!d1.Empty());
    ASSERT_TRUE(d1.GetRemoteId() == REMOTE_ID);
    d1(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);
    ASSERT_TRUE(transport.m_lastId == REMOTE_ID);

    // Named raw lambda (auto, not std::function)
    auto rawLam = [](int i) { ASSERT_TRUE(i == TEST_INT); };
    auto d2 = MakeDelegate(rawLam, REMOTE_ID, channel);
    ASSERT_TRUE(!d2.Empty());
    d2(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 2);

    // Capturing lambda
    int captured = TEST_INT;
    auto d3 = MakeDelegate([captured](int i) { ASSERT_TRUE(i == captured); }, REMOTE_ID, channel);
    ASSERT_TRUE(!d3.Empty());
    d3(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 3);
}

// ---- Send-only channel: ctor ID, no Bind() required on the sender --------------

static void RemoteChannel_SendOnly_CtorId()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;

    // Default ctor ID is invalid until set
    RemoteChannel<void(int)> unbound(transport, serializer);
    ASSERT_TRUE(unbound.GetRemoteId() == INVALID_REMOTE_ID);

    // Sender: ID from ctor, never calls Bind()
    RemoteChannel<void(int)> tx(transport, serializer, REMOTE_ID);
    ASSERT_TRUE(tx.GetRemoteId() == REMOTE_ID);

    tx(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);
    ASSERT_TRUE(transport.m_lastId == REMOTE_ID);

    // Receiver: separate channel binds a handler to the same ID
    g_invoked = false;
    g_lastInt = 0;
    RemoteChannel<void(int)> rx(transport, serializer);
    rx.Bind(&FreeFuncInt, REMOTE_ID);

    auto recvStream = transport.GetPayloadStream();
    recvStream.seekg(0);
    rx.GetEndpoint()->Invoke(recvStream);

    ASSERT_TRUE(g_invoked);
    ASSERT_TRUE(g_lastInt == TEST_INT);
}

// ---- Raw lambda Bind on receive side with round-trip ---------------------------

static void RemoteChannel_Bind_RawLambda()
{
    MockTransport transport;
    RCSerializer<void(int)> serializer;
    RemoteChannel<void(int)> channel(transport, serializer);

    g_invoked = false;
    channel.Bind([](int i) {
        g_invoked = true;
        ASSERT_TRUE(i == TEST_INT);
    }, REMOTE_ID);

    // Send side: dispatch a value through the channel
    auto sender = MakeDelegate(&FreeFuncInt, REMOTE_ID, channel);
    sender(TEST_INT);
    ASSERT_TRUE(transport.m_sendCount == 1);

    // Receive side: invoke through the channel's endpoint
    auto recvStream = transport.GetPayloadStream();
    recvStream.seekg(0);
    channel.GetEndpoint()->Invoke(recvStream);

    ASSERT_TRUE(g_invoked);
}

// ---- Entry point ---------------------------------------------------------------

void RemoteChannelTests()
{
    RemoteChannel_Construction();

    RemoteChannel_MakeDelegate_FreeFunc();
    RemoteChannel_MakeDelegate_MemberFunc();
    RemoteChannel_MakeDelegate_ConstMemberFunc();
    RemoteChannel_MakeDelegate_SharedPtrMemberFunc();
    RemoteChannel_MakeDelegate_StdFunction();
    RemoteChannel_MakeDelegate_RawLambda();

    RemoteChannel_RoundTrip_FreeFunc_Int();
    RemoteChannel_RoundTrip_FreeFunc_IntRef();
    RemoteChannel_RoundTrip_FreeFunc_IntPtr();
    RemoteChannel_RoundTrip_FreeFunc_RemoteData();
    RemoteChannel_RoundTrip_FreeFunc_TwoArgs();
    RemoteChannel_RoundTrip_MemberFunc();
    RemoteChannel_RoundTrip_MemberFunc_RemoteData();
    RemoteChannel_RoundTrip_SharedPtrMemberFunc();

    RemoteChannel_DispatchError_PropagatedToErrorHandler();
    RemoteChannel_MakeDelegate_MatchesManualWiring();
    RemoteChannel_SendOnly_CtorId();
    RemoteChannel_Bind_RawLambda();
}
