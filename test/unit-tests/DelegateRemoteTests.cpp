#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include <iostream>
#include <set>
#include <cstring>
#include <thread>

using namespace dmq;
using namespace dmq::os;
using namespace std;
using namespace UnitTestData;

namespace Remote
{
    const int REMOTE_ID = 1;

    struct TestReturn
    {
        ~TestReturn()
        {
            val++;
        }
        static int val;
    };

    int TestReturn::val = 0;

    class TestReturnClass
    {
    public:
        TestReturn Func() { return TestReturn{}; }
    };
    
    void FreeFuncInt(int i) { ASSERT_TRUE(i == TEST_INT); }
    void FreeFuncIntRef(int& i) { ASSERT_TRUE(i == TEST_INT); }
    void FreeFuncIntPtr(int* pi) { 
        ASSERT_TRUE(pi); 
        ASSERT_TRUE(*pi == TEST_INT); 
    }
    void FreeFuncConstIntPtr(const int* pi) { 
        ASSERT_TRUE(pi); 
        ASSERT_TRUE(*pi == TEST_INT); 
    }
    void FreeFuncConstIntRef(const int& i) {
        ASSERT_TRUE(i == TEST_INT);
    }
    void FreeFuncIntPtrPtr(int** pi) {
        ASSERT_TRUE(pi);
        ASSERT_TRUE(*pi);
        ASSERT_TRUE(**pi == TEST_INT);
    }

    class RemoteData {
    public:
        RemoteData(int _x, int _y) : x(_x), y(_y) {}
        RemoteData() {}
        int x = 0;
        int y = 0;
    };

    std::ostream& operator<<(std::ostream& os, const RemoteData& data) {
        os << data.x << endl;
        os << data.y << endl;
        return os;
    }

    std::istream& operator>>(std::istream& is, RemoteData& data) {
        is >> data.x;
        is >> data.y;
        return is;
    }

    void FreeFuncRemoteData(RemoteData& d) { 
        ASSERT_TRUE(d.x == TEST_INT); 
        ASSERT_TRUE(d.y == TEST_INT+1);
    }

    void FreeFuncRemoteDataPtr(RemoteData* d) {
        ASSERT_TRUE(d->x == TEST_INT);
        ASSERT_TRUE(d->y == TEST_INT + 1);
    }

    class RemoteClass {
    public: 
        void MemberFuncInt(int i) { ASSERT_TRUE(i == TEST_INT); }
        void MmberFuncIntRef(int& i) { ASSERT_TRUE(i == TEST_INT); }
        void MemberFuncIntPtr(int* pi) {
            ASSERT_TRUE(pi);
            ASSERT_TRUE(*pi == TEST_INT);
        }
        void MemberFuncRemoteData(RemoteData& d) {
            ASSERT_TRUE(d.x == TEST_INT);
            ASSERT_TRUE(d.y == TEST_INT + 1);
        }
        void MemberFuncRemoteDataPtr(RemoteData* d) {
            ASSERT_TRUE(d->x == TEST_INT);
            ASSERT_TRUE(d->y == TEST_INT + 1);
        }
    };

    // Do not allow shared_ptr references. Causes compile error if used with Remote delegates.
    void NoError(std::shared_ptr<const StructParam> c) {}
    void NoError2(const std::string& s, std::shared_ptr<const StructParam> c) {}
    void Error(std::shared_ptr<const StructParam>& c) {}
    void Error2(const std::shared_ptr<const StructParam>& c) {}
    void Error3(std::shared_ptr<const StructParam>* c) {}
    void Error4(const std::shared_ptr<const StructParam>* c) {}

    class ClassError
    {
    public:
        // Do not allow shared_ptr references. Causes compile error if used with Remote delegates.
        void NoError(std::shared_ptr<const StructParam> c) {}
        void NoError2(const std::string& s, std::shared_ptr<const StructParam> c) {}
        void Error(std::shared_ptr<const StructParam>& c) {}
        void Error2(const std::shared_ptr<const StructParam>& c) {}
        void Error3(std::shared_ptr<const StructParam>* c) {}
        void Error4(const std::shared_ptr<const StructParam>* c) {}
    };

    class MockDispatcher : public IDispatcher
    {
    public:
        MockDispatcher() : m_ss(ios::in | ios::out | ios::binary) {}

        virtual int Dispatch(dmq::xostringstream& os, dmq::DelegateRemoteId id, uint16_t* outSeqNum = nullptr) override {
            // Save dispatched string to simulate sending data over a transport
            m_ss.str("");
            m_ss.clear();
            m_ss << os.str();
            os.str("");
            os.clear();
            return 0;
        }

        std::stringstream& GetDispached() { return m_ss; }

    private:
        std::stringstream m_ss;
    };

    template<typename... Ts>
    void make_serialized(std::ostream& os) { }

    template<typename Arg1, typename... Args>
    void make_serialized(std::ostream& os, Arg1& arg1, Args... args) {
        os << arg1; 
        make_serialized(os, args...);
    }

    template<typename Arg1, typename... Args>
    void make_serialized(std::ostream& os, Arg1* arg1, Args... args) {
        os << *arg1;
        make_serialized(os, args...);
    }

    template<typename Arg1, typename... Args>
    void make_serialized(std::ostream& os, Arg1** arg1, Args... args) {
        static_assert(!std::is_pointer_v<Arg1>, "Pointer-to-pointer argument not supported");
    }

    template<typename... Ts>
    void make_unserialized(std::istream& is) { }

    template<typename Arg1, typename... Args>
    void make_unserialized(std::istream& is, Arg1& arg1, Args&&... args) {
        is >> const_cast<std::remove_const_t<Arg1>&>(arg1);
        make_unserialized(is, args...);
    }

    template<typename Arg1, typename... Args>
    void make_unserialized(std::istream& is, Arg1* arg1, Args&&... args) {
        is >> const_cast<std::remove_const_t<Arg1>&>(*arg1);
        make_unserialized(is, args...);
    }

    template<typename Arg1, typename... Args>
    void make_unserialized(std::istream& is, Arg1** arg1, Args&&... args) {
        static_assert(!std::is_pointer_v<Arg1>, "Pointer-to-pointer argument not supported");
    }

    template <class R>
    struct Serializer; // Not defined

    template<class RetType, class... Args>
    class Serializer<RetType(Args...)> : public ISerializer<RetType(Args...)>
    {
    public:
        virtual std::ostream& Write(std::ostream& os, const Args&... args) override {
            make_serialized(os, args...);
            return os;
        }

        virtual std::istream& Read(std::istream& is, Args&... args) override {
            make_unserialized(is, args...);
            return is;
        }
    };
}
using namespace Remote;

static void DelegateFreeRemoteTests()
{
    using Del = DelegateFreeRemote<void(int)>;

    Del delegate1(FreeFuncInt1, REMOTE_ID);

    // Invoking a remote delegate with no serializer/stream configured raises
    // ERR_NO_SERIALIZER through the error handler (no throw when handled).
    delegate1.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    delegate1(TEST_INT);
    ASSERT_TRUE(delegate1.GetError() == DelegateError::ERR_NO_SERIALIZER);
    std::invoke(delegate1, TEST_INT);
    ASSERT_TRUE(delegate1.GetError() == DelegateError::ERR_NO_SERIALIZER);

    auto delegate2 = delegate1;
    ASSERT_TRUE(delegate1 == delegate2);
    ASSERT_TRUE(!delegate1.Empty());
    ASSERT_TRUE(!delegate2.Empty());

    Del delegate3(FreeFuncInt1, REMOTE_ID);
    delegate3 = delegate1;
    ASSERT_TRUE(delegate3 == delegate1);
    ASSERT_TRUE(delegate3);

    delegate3.Clear();
    ASSERT_TRUE(delegate3.Empty());
    ASSERT_TRUE(!delegate3);

    auto* delegate4 = delegate1.Clone();
    ASSERT_TRUE(*delegate4 == delegate1);
    delete delegate4;

    auto delegate5 = std::move(delegate1);
    ASSERT_TRUE(!delegate5.Empty());
    ASSERT_TRUE(delegate1.Empty());

    Del delegate6(FreeFuncInt1, REMOTE_ID);
    delegate6 = std::move(delegate2);
    ASSERT_TRUE(!delegate6.Empty());
    ASSERT_TRUE(delegate2.Empty());
    ASSERT_TRUE(delegate6 != nullptr);
    ASSERT_TRUE(nullptr != delegate6);
    ASSERT_TRUE(delegate2 == nullptr);
    ASSERT_TRUE(nullptr == delegate2);

    // Compare disparate delegate types
    DelegateFunction<void(int)> other;
    ASSERT_TRUE(!(delegate6.Equal(other)));

    delegate6 = nullptr;
    ASSERT_TRUE(delegate6.Empty());
    ASSERT_TRUE(delegate6 == nullptr);

    // Make sure we get a default constructed return value.
    TestReturn::val = 0;
    DelegateFreeRemote<TestReturn()> testRet;
    testRet.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    ASSERT_TRUE(TestReturn::val == 0);
    testRet();
    ASSERT_TRUE(TestReturn::val == 1);

    DelegateFreeRemote<std::unique_ptr<int>(int)> delUnique;
    delUnique.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    auto tmp = delUnique(10);
    ASSERT_TRUE(tmp == nullptr);

    // Test const pointer and reference arguments
    {
        MockDispatcher dispatcher;
        Serializer<void(const int*)> serializer;
        xostringstream os(ios::in | ios::out | ios::binary);
        auto d = MakeDelegate(&FreeFuncConstIntPtr, REMOTE_ID);
        d.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int){}));
        d.SetStream(&os);
        d.SetDispatcher(&dispatcher);
        d.SetSerializer(&serializer);
        
        int val = TEST_INT;
        d(&val);

        std::stringstream& ss = dispatcher.GetDispached();
        DelegateFreeRemote<void(const int*)> dest(&FreeFuncConstIntPtr, REMOTE_ID);
        dest.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int){}));
        dest.SetSerializer(&serializer);
        dest.Invoke(ss);
    }

    {
        MockDispatcher dispatcher;
        Serializer<void(const int&)> serializer;
        xostringstream os(ios::in | ios::out | ios::binary);
        auto d = MakeDelegate(&FreeFuncConstIntRef, REMOTE_ID);
        d.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int){}));
        d.SetStream(&os);
        d.SetDispatcher(&dispatcher);
        d.SetSerializer(&serializer);
        
        int val = TEST_INT;
        d(val);

        std::stringstream& ss = dispatcher.GetDispached();
        DelegateFreeRemote<void(const int&)> dest(&FreeFuncConstIntRef, REMOTE_ID);
        dest.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int){}));
        dest.SetSerializer(&serializer);
        dest.Invoke(ss);
    }
    delUnique.Bind(&FuncUnique, REMOTE_ID);
    std::unique_ptr<int> up = delUnique(12);
    ASSERT_TRUE(up == nullptr);  // Remote delegate has default return value 

    auto delS1 = MakeDelegate(FreeFuncInt1, REMOTE_ID);
    auto delS2 = MakeDelegate(FreeFuncInt1_2, REMOTE_ID);
    ASSERT_TRUE(!(delS1 == delS2));

    std::set<Del> setDel;
    setDel.insert(delS1);
    setDel.insert(delS2);
    ASSERT_TRUE(setDel.size() == 2);

    delS1.Clear();
    ASSERT_TRUE(delS1.Empty());
    std::swap(delS1, delS2);
    ASSERT_TRUE(!delS1.Empty());
    ASSERT_TRUE(delS2.Empty());

    // Error handler is copied with the delegate into std::function, so the
    // unconfigured invoke reports ERR_NO_SERIALIZER instead of throwing.
    auto stdFuncDel = MakeDelegate(&FreeFuncIntWithReturn1, REMOTE_ID);
    stdFuncDel.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    std::function<int(int)> stdFunc = stdFuncDel;
    int stdFuncRetVal = stdFunc(TEST_INT);
    ASSERT_TRUE(stdFuncRetVal == 0);

#if 0
    // ClassSingleton private constructor. Can't use singleton as ref (&),
    // pointer (*), or pointer-to-pointer (**) since remote delegate makes 
    // copy of ClassSingleton argument.
    auto& singleton = ClassSingleton::GetInstance();
    auto delRef = MakeDelegate(&SetClassSingletonRef, REMOTE_ID);
    auto delPtr = MakeDelegate(&SetClassSingletonPtr, REMOTE_ID);
    auto delPtrPtr = MakeDelegate(&SetClassSingletonPtrPtr, REMOTE_ID);
#endif

    auto noErrorDel = MakeDelegate(&NoError, REMOTE_ID);
    auto noErrorDel2 = MakeDelegate(&NoError2, REMOTE_ID);
#if 0
    // Causes compiler error. shared_ptr references not allowed; undefined behavior 
    // in multi-threaded system.
    auto errorDel = MakeDelegate(&Error, REMOTE_ID);
    auto errorDel2 = MakeDelegate(&Error2, REMOTE_ID);
    auto errorDel3 = MakeDelegate(&Error3, REMOTE_ID);
    auto errorDel4 = MakeDelegate(&Error4, REMOTE_ID);
#endif

    // Shared pointer does not copy singleton object; no copy of shared_ptr arg.
    auto singletonSp = ClassSingleton::GetInstanceSp();
    auto delShared = MakeDelegate(&SetClassSingletonShared, REMOTE_ID);
    delShared.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    delShared(singletonSp);

#if 0  // Arg** not supported on remote delegate
    // Test nullptr arguments
    auto nullPtrArg = MakeDelegate(&NullPtrArg, REMOTE_ID);
    nullPtrArg(nullptr);
    auto nullPtrPtrArg = MakeDelegate(&NullPtrPtrArg, REMOTE_ID);
    nullPtrPtrArg(nullptr);
#endif

    // Test outgoing ptr argument
    // Target function does *not* change local instance data. A copy of delegate
    // and all arguments is sent to the destination thread.
    StructParam sparam;
    int iparam = 100;
    sparam.val = TEST_INT;
    auto outgoingArg = MakeDelegate(&OutgoingPtrArg, REMOTE_ID);
    outgoingArg.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    outgoingArg(&sparam, &iparam);
    ASSERT_TRUE(sparam.val == TEST_INT);
    ASSERT_TRUE(iparam == 100);

#if 0  // Arg** not supported on remote delegate
    // Test outgoing ptr-ptr argument
    StructParam* psparam = nullptr;
    auto outgoingArg2 = MakeDelegate(&OutgoingPtrPtrArg, REMOTE_ID);
    // outgoingArg2(&psparam);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(psparam == nullptr);
#endif

    // Test outgoing ref argument
    sparam.val = TEST_INT;
    auto outgoingArg3 = MakeDelegate(&OutgoingRefArg, REMOTE_ID);
    outgoingArg3.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    outgoingArg3(sparam);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(sparam.val == TEST_INT);
    
    {
        MockDispatcher dispatcher;
        Remote::Serializer<void(Class* c)> serializer;

        // Remote invoke does not copy Class object when passed to func
        Class classInstance;
        Class::m_construtorCnt = 0;
        auto cntDel = MakeDelegate(&ConstructorCnt, REMOTE_ID);
        cntDel.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
        cntDel.SetDispatcher(&dispatcher);
        cntDel.SetSerializer(&serializer);
        cntDel(&classInstance);
        ASSERT_TRUE(Class::m_construtorCnt == 0);
    }

    // Compile error. Invalid to pass void* argument to remote target function
#if 0
    // Test void* args
    const char* str = "Hello World!";
    void* voidPtr = (void*)str;
    auto voidPtrNotNullDel = MakeDelegate(&VoidPtrArgNotNull, REMOTE_ID);
    voidPtrNotNullDel(voidPtr);
    auto voidPtrNullDel = MakeDelegate(&VoidPtrArgNull, REMOTE_ID);
    voidPtrNullDel(nullptr);
#endif

    // Test void* return. Return value is nullptr because return 
    // value is invalid on a non-blocking remote delegate
    auto retVoidPtrDel = MakeDelegate(&RetVoidPtr, REMOTE_ID);
    retVoidPtrDel.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    auto retVoidPtr = retVoidPtrDel();
    ASSERT_TRUE(retVoidPtr == nullptr);
    const char* retStr = (const char*)retVoidPtr;
    ASSERT_TRUE(retStr == nullptr);

#if 0
    // Invalid: Can't pass a && argument through a message queue
    // Test rvalue ref
    auto rvalueRefDel = MakeDelegate(&FuncRvalueRef, REMOTE_ID);
    int rv = TEST_INT;
    rvalueRefDel(std::move(rv));
    rvalueRefDel(12345678);
#endif

    // Array of delegates
    Del* arr = new Del[2];
    arr[0].Bind(FreeFuncInt1, REMOTE_ID);
    arr[1].Bind(FreeFuncInt1, REMOTE_ID);
    for (int i = 0; i < 2; i++) {
        arr[i].SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
        arr[i](TEST_INT);
    }
    delete[] arr;

    std::function<void(DelegateRemoteId, DelegateError, int)> errorHandler = [](DelegateRemoteId id, DelegateError error, DelegateErrorAux code) {
        if (error != dmq::DelegateError::SUCCESS)
            ASSERT_TRUE(false);
    };
    std::function<void(DelegateRemoteId, DelegateError, int)> errorHandlerNoAssert = [](DelegateRemoteId id, DelegateError error, DelegateErrorAux code) {};
    MockDispatcher dispatcher;
    xostringstream os(ios::in | ios::out | ios::binary);

    {
        Remote::Serializer<void(int)> serializer;
        DelegateFreeRemote<void(int)> delegateRemote(FreeFuncInt, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        delegateRemote(TEST_INT);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::SUCCESS);

        delegateRemote.SetErrorHandler(MakeDelegate(errorHandlerNoAssert));
        recv_stream.setstate(std::ios::failbit);
        delegateRemote.Invoke(recv_stream);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::ERR_STREAM_NOT_GOOD);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::SUCCESS);
    }

    {
        Remote::Serializer<void(int&)> serializer;
        DelegateFreeRemote<void(int&)> delegateRemote(FreeFuncIntRef, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        int i = TEST_INT;
        delegateRemote(i);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

    {
        Remote::Serializer<void(int*)> serializer;
        DelegateFreeRemote<void(int*)> delegateRemote(FreeFuncIntPtr, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        int i = TEST_INT;
        delegateRemote(&i);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

#if 0  // Pointer-to-pointer argument not supported
    {
        Remote::Serializer<void(int**)> serializer;
        DelegateFreeRemote<void(int**)> delegateRemote(FreeFuncIntPtrPtr, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        int i = TEST_INT;
        int* pi = &i;
        delegateRemote(&pi);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }
#endif

    {
        Remote::Serializer<void(RemoteData&)> serializer;
        DelegateFreeRemote<void(RemoteData&)> delegateRemote(FreeFuncRemoteData, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        RemoteData d(TEST_INT, TEST_INT+1);
        delegateRemote(d);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

    {
        Remote::Serializer<void(RemoteData*)> serializer;
        DelegateFreeRemote<void(RemoteData*)> delegateRemote(FreeFuncRemoteDataPtr, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        RemoteData d(TEST_INT, TEST_INT + 1);
        delegateRemote(&d);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }
}

static void DelegateMemberRemoteTests()
{
    using Del = DelegateMemberRemote<TestClass1, void(int)>;

    TestClass1 testClass1;

    Del delegate1(&testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    delegate1.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    delegate1(TEST_INT);
    ASSERT_TRUE(delegate1.GetError() == DelegateError::ERR_NO_SERIALIZER);
    std::invoke(delegate1, TEST_INT);
    ASSERT_TRUE(delegate1.GetError() == DelegateError::ERR_NO_SERIALIZER);

    auto delegate2 = delegate1;
    ASSERT_TRUE(delegate1 == delegate2);
    ASSERT_TRUE(!delegate1.Empty());
    ASSERT_TRUE(!delegate2.Empty());

    Del delegate3(&testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    delegate3 = delegate1;
    ASSERT_TRUE(delegate3 == delegate1);
    ASSERT_TRUE(delegate3);

    delegate3.Clear();
    ASSERT_TRUE(delegate3.Empty());
    ASSERT_TRUE(!delegate3);

    auto* delegate4 = delegate1.Clone();
    ASSERT_TRUE(*delegate4 == delegate1);
    delete delegate4;

    auto delegate5 = std::move(delegate1);
    ASSERT_TRUE(!delegate5.Empty());
    ASSERT_TRUE(delegate1.Empty());

    Del delegate6(&testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    delegate6 = std::move(delegate2);
    ASSERT_TRUE(!delegate6.Empty());
    ASSERT_TRUE(delegate2.Empty());
    ASSERT_TRUE(delegate6 != nullptr);
    ASSERT_TRUE(nullptr != delegate6);
    ASSERT_TRUE(delegate2 == nullptr);
    ASSERT_TRUE(nullptr == delegate2);

    // Compare disparate delegate types
    DelegateFunction<void(int)> other;
    ASSERT_TRUE(!(delegate6.Equal(other)));

    delegate6 = nullptr;
    ASSERT_TRUE(delegate6.Empty());
    ASSERT_TRUE(delegate6 == nullptr);

    // Check for const correctness
    const Class c;
    DelegateMemberRemote<const Class, std::uint16_t(void)> dConstClass;
    dConstClass.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    //dConstClass.Bind(&c, &Class::Func, REMOTE_ID);     // Not OK. Should fail compile.
    dConstClass.Bind(&c, &Class::FuncConst, REMOTE_ID);  // OK
    auto rConst = dConstClass();

    // Make sure we get a default constructed return value.
    TestReturn::val = 0;
    DelegateMemberRemote<TestReturnClass, TestReturn()> testRet;
    testRet.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    ASSERT_TRUE(TestReturn::val == 0);
    testRet();
    ASSERT_TRUE(TestReturn::val == 1);

    Class c2;
    DelegateMemberRemote<Class, std::unique_ptr<int>(int)> delUnique;
    delUnique.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    auto tmp = delUnique(10);
    ASSERT_TRUE(tmp == nullptr);
    delUnique.Bind(&c2, &Class::FuncUnique, REMOTE_ID);
    std::unique_ptr<int> up = delUnique(12);
    ASSERT_TRUE(up == nullptr);  // Remote delegate has default return value 

    auto delS1 = MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    auto delS2 = MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1_2, REMOTE_ID);
    ASSERT_TRUE(!(delS1 == delS2));

#if 0  // DelegateMemberRemote can't be inserted into ordered container
    std::set<Del> setDel;
    setDel.insert(delS1);
    setDel.insert(delS2);
    ASSERT_TRUE(setDel.size() == 2);
#endif

    const TestClass1 tcConst;
    auto delConstCheck = MakeDelegate(&tcConst, &TestClass1::ConstCheck, REMOTE_ID);
    delConstCheck.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    auto delConstCheckRetVal = delConstCheck(TEST_INT);
    ASSERT_TRUE(delConstCheckRetVal == 0);

    delS1.Clear();
    ASSERT_TRUE(delS1.Empty());
    std::swap(delS1, delS2);
    ASSERT_TRUE(!delS1.Empty());
    ASSERT_TRUE(delS2.Empty());

    auto stdFuncDel = MakeDelegate(&testClass1, &TestClass1::MemberFuncIntWithReturn1, REMOTE_ID);
    stdFuncDel.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    std::function<int(int)> stdFunc = stdFuncDel;
    int stdFuncRetVal = stdFunc(TEST_INT);
    ASSERT_TRUE(stdFuncRetVal == 0);

    SetClassSingleton setClassSingleton;
#if 0
    // ClassSingleton private constructor. Can't use singleton as ref (&),
    // pointer (*), or pointer-to-pointer (**) since remote delegate makes 
    // copy of ClassSingleton argument.
    auto& singleton = ClassSingleton::GetInstance();
    auto delRef = MakeDelegate(&setClassSingleton, &SetClassSingleton::Ref, REMOTE_ID);
    auto delPtr = MakeDelegate(&setClassSingleton, &SetClassSingleton::Ptr, REMOTE_ID);
    auto delPtrPtr = MakeDelegate(&setClassSingleton, &SetClassSingleton::PtrPtr, REMOTE_ID);
#endif

    ClassError classError;
    auto noErrorDel = MakeDelegate(&classError, &ClassError::NoError, REMOTE_ID);
    auto noErrorDel2 = MakeDelegate(&classError, &ClassError::NoError2, REMOTE_ID);
#if 0
    // Causes compiler error. shared_ptr references not allowed; undefined behavior 
    // in multi-threaded system.
    auto errorDel = MakeDelegate(&classError, &ClassError::Error, REMOTE_ID);
    auto errorDel2 = MakeDelegate(&classError, &ClassError::Error2, REMOTE_ID);
    auto errorDel3 = MakeDelegate(&classError, &ClassError::Error3, REMOTE_ID);
    auto errorDel4 = MakeDelegate(&classError, &ClassError::Error4, REMOTE_ID);
#endif

    // Shared pointer does not copy singleton object; no copy of shared_ptr arg.
    auto singletonSp = ClassSingleton::GetInstanceSp();
    auto delShared = MakeDelegate(&setClassSingleton, &SetClassSingleton::Shared, REMOTE_ID);
    delShared.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    delShared(singletonSp);

    Class voidTest;
    // Compile error. Invalid to pass void* argument to remote target function
#if 0
    // Test void* args
    const char* str = "Hello World!";
    void* voidPtr = (void*)str;
    auto voidPtrNotNullDel = MakeDelegate(&voidTest, &Class::VoidPtrArgNotNull, REMOTE_ID);
    voidPtrNotNullDel(voidPtr);
    auto voidPtrNullDel = MakeDelegate(&voidTest, &Class::VoidPtrArgNull, REMOTE_ID);
    voidPtrNullDel(nullptr);
#endif

    // Test void* return. Return value is nullptr because return 
    // value is invalid on a non-blocking remote delegate
    auto retVoidPtrDel = MakeDelegate(&voidTest, &Class::RetVoidPtr, REMOTE_ID);
    retVoidPtrDel.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    auto retVoidPtr = retVoidPtrDel();
    ASSERT_TRUE(retVoidPtr == nullptr);
    const char* retStr = (const char*)retVoidPtr;
    ASSERT_TRUE(retStr == nullptr);

    // Array of delegates
    Del* arr = new Del[2];
    arr[0].Bind(&testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    arr[1].Bind(&testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    for (int i = 0; i < 2; i++) {
        arr[i].SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
        arr[i](TEST_INT);
    }
    delete[] arr;

    std::function<void(DelegateRemoteId, DelegateError, DelegateErrorAux)> errorHandler = [](DelegateRemoteId id, DelegateError error, DelegateErrorAux code) {
        if (error != dmq::DelegateError::SUCCESS)
            ASSERT_TRUE(false);
    };
    std::function<void(DelegateRemoteId, DelegateError, int)> errorHandlerNoAssert = [](DelegateRemoteId id, DelegateError error, DelegateErrorAux code) {};
    MockDispatcher dispatcher;
    RemoteClass remoteClass;
    xostringstream os(ios::in | ios::out | ios::binary);

    {
        Remote::Serializer<void(int)> serializer;
        DelegateMemberRemote<RemoteClass, void(int)> delegateRemote(&remoteClass, &RemoteClass::MemberFuncInt, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        delegateRemote(TEST_INT);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::SUCCESS);

        delegateRemote.SetErrorHandler(MakeDelegate(errorHandlerNoAssert));
        recv_stream.setstate(std::ios::failbit);
        delegateRemote.Invoke(recv_stream);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::ERR_STREAM_NOT_GOOD);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::SUCCESS);
    }

    {
        Remote::Serializer<void(int&)> serializer;
        DelegateMemberRemote<RemoteClass, void(int&)> delegateRemote(&remoteClass, &RemoteClass::MmberFuncIntRef, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        int i = TEST_INT;
        delegateRemote(i);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

    {
        Remote::Serializer<void(int*)> serializer;
        DelegateMemberRemote<RemoteClass, void(int*)> delegateRemote(&remoteClass, &RemoteClass::MemberFuncIntPtr, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        int i = TEST_INT;
        delegateRemote(&i);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

    {
        Remote::Serializer<void(RemoteData&)> serializer;
        DelegateMemberRemote<RemoteClass, void(RemoteData&)> delegateRemote(&remoteClass, &RemoteClass::MemberFuncRemoteData, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        RemoteData d(TEST_INT, TEST_INT + 1);
        delegateRemote(d);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

    {
        Remote::Serializer<void(RemoteData*)> serializer;
        DelegateMemberRemote<RemoteClass, void(RemoteData*)> delegateRemote(&remoteClass, &RemoteClass::MemberFuncRemoteDataPtr, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        RemoteData d(TEST_INT, TEST_INT + 1);
        delegateRemote(&d);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }
}

static void DelegateMemberSpRemoteTests()
{
    using Del = DelegateMemberRemote<TestClass1, void(int)>;

    auto testClass1 = std::make_shared<TestClass1>();

    Del delegate1(testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    ASSERT_TRUE(delegate1.GetError() == DelegateError::SUCCESS);
    delegate1.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    delegate1(TEST_INT);
    ASSERT_TRUE(delegate1.GetError() == DelegateError::ERR_NO_SERIALIZER);
    std::invoke(delegate1, TEST_INT);
    ASSERT_TRUE(delegate1.GetError() == DelegateError::ERR_NO_SERIALIZER);

    auto delegate2 = delegate1;
    ASSERT_TRUE(delegate1 == delegate2);
    ASSERT_TRUE(!delegate1.Empty());
    ASSERT_TRUE(!delegate2.Empty());

    Del delegate3(testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    delegate3 = delegate1;
    ASSERT_TRUE(delegate3 == delegate1);
    ASSERT_TRUE(delegate3);

    delegate3.Clear();
    ASSERT_TRUE(delegate3.Empty());
    ASSERT_TRUE(!delegate3);

    auto* delegate4 = delegate1.Clone();
    ASSERT_TRUE(*delegate4 == delegate1);
    delete delegate4;

    auto delegate5 = std::move(delegate1);
    ASSERT_TRUE(!delegate5.Empty());
    ASSERT_TRUE(delegate1.Empty());

    Del delegate6(testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    delegate6 = std::move(delegate2);
    ASSERT_TRUE(!delegate6.Empty());
    ASSERT_TRUE(delegate2.Empty());
    ASSERT_TRUE(delegate6 != nullptr);
    ASSERT_TRUE(nullptr != delegate6);
    ASSERT_TRUE(delegate2 == nullptr);
    ASSERT_TRUE(nullptr == delegate2);

    // Compare disparate delegate types
    DelegateFunction<void(int)> other;
    ASSERT_TRUE(!(delegate6.Equal(other)));

    delegate6 = nullptr;
    ASSERT_TRUE(delegate6.Empty());
    ASSERT_TRUE(delegate6 == nullptr);

    // Check for const correctness
    auto c = std::make_shared<const Class>();
    DelegateMemberRemote<const Class, std::uint16_t(void)> dConstClass;
    dConstClass.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    //dConstClass.Bind(c, &Class::Func, REMOTE_ID);     // Not OK. Should fail compile.
    dConstClass.Bind(c, &Class::FuncConst, REMOTE_ID);  // OK
    auto rConst = dConstClass();

    // Make sure we get a default constructed return value.
    TestReturn::val = 0;
    DelegateMemberRemote<TestReturnClass, TestReturn()> testRet;
    testRet.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    ASSERT_TRUE(TestReturn::val == 0);
    testRet();
    ASSERT_TRUE(TestReturn::val == 1);

    auto c2 = std::make_shared<Class>();
    DelegateMemberRemote<Class, std::unique_ptr<int>(int)> delUnique;
    delUnique.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    auto tmp = delUnique(10);
    ASSERT_TRUE(tmp == nullptr);
    delUnique.Bind(c2, &Class::FuncUnique, REMOTE_ID);
    std::unique_ptr<int> up = delUnique(12);
    ASSERT_TRUE(up == nullptr);  // Remote delegate has default return value 

    auto delS1 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    auto delS2 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1_2, REMOTE_ID);
    ASSERT_TRUE(!(delS1 == delS2));

#if 0  // DelegateMemberRemote can't be inserted into ordered container
    std::set<Del> setDel;
    setDel.insert(delS1);
    setDel.insert(delS2);
    ASSERT_TRUE(setDel.size() == 2);
#endif

    delS1.Clear();
    ASSERT_TRUE(delS1.Empty());
    std::swap(delS1, delS2);
    ASSERT_TRUE(!delS1.Empty());
    ASSERT_TRUE(delS2.Empty());

    auto stdFuncDel = MakeDelegate(testClass1, &TestClass1::MemberFuncIntWithReturn1, REMOTE_ID);
    stdFuncDel.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    std::function<int(int)> stdFunc = stdFuncDel;
    int stdFuncRetVal = stdFunc(TEST_INT);
    ASSERT_TRUE(stdFuncRetVal == 0);

    // Array of delegates
    Del* arr = new Del[2];
    arr[0].Bind(testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    arr[1].Bind(testClass1, &TestClass1::MemberFuncInt1, REMOTE_ID);
    for (int i = 0; i < 2; i++) {
        arr[i].SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
        arr[i](TEST_INT);
    }
    delete[] arr;

    std::function<void(DelegateRemoteId, DelegateError, int)> errorHandler = [](DelegateRemoteId id, DelegateError error, int code) {
        if (error != dmq::DelegateError::SUCCESS)
            ASSERT_TRUE(false);
    };
    std::function<void(DelegateRemoteId, DelegateError, int)> errorHandlerNoAssert = [](DelegateRemoteId id, DelegateError error, DelegateErrorAux code) {};
    static std::function<void(int)> LambdaFuncInt = +[](int i) {
        ASSERT_TRUE(i == TEST_INT);
    };
    static std::function<void(int&)> LambdaFuncIntRef = +[](int& i) {
        ASSERT_TRUE(i == TEST_INT);
    };
    static std::function<void(int*)> LambdaFuncIntPtr = +[](int* i) {
        ASSERT_TRUE(i);
        ASSERT_TRUE(*i == TEST_INT);
    };
    static std::function<void(RemoteData&)> LambdaFuncRemoteData = +[](RemoteData& d) {
        ASSERT_TRUE(d.x == TEST_INT);
        ASSERT_TRUE(d.y == TEST_INT+1);
    };
    static std::function<void(RemoteData*)> LambdaFuncRemoteDataPtr = +[](RemoteData* d) {
        ASSERT_TRUE(d);
        ASSERT_TRUE(d->x == TEST_INT);
        ASSERT_TRUE(d->y == TEST_INT + 1);
    };
    MockDispatcher dispatcher;
    xostringstream os(ios::in | ios::out | ios::binary);

    {
        Remote::Serializer<void(int)> serializer;
        DelegateFunctionRemote<void(int)> delegateRemote(LambdaFuncInt, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        delegateRemote(TEST_INT);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::SUCCESS);

        delegateRemote.SetErrorHandler(MakeDelegate(errorHandlerNoAssert));
        recv_stream.setstate(std::ios::failbit);
        delegateRemote.Invoke(recv_stream);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::ERR_STREAM_NOT_GOOD);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::SUCCESS);
    }

    {
        Remote::Serializer<void(int&)> serializer;
        DelegateFunctionRemote<void(int&)> delegateRemote(LambdaFuncIntRef, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        int i = TEST_INT;
        delegateRemote(i);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

    {
        Remote::Serializer<void(int*)> serializer;
        DelegateFunctionRemote<void(int*)> delegateRemote(LambdaFuncIntPtr, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        int i = TEST_INT;
        delegateRemote(&i);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

    {
        Remote::Serializer<void(RemoteData&)> serializer;
        DelegateFunctionRemote<void(RemoteData&)> delegateRemote(LambdaFuncRemoteData, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        RemoteData d(TEST_INT, TEST_INT + 1);
        delegateRemote(d);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

    {
        Remote::Serializer<void(RemoteData*)> serializer;
        DelegateFunctionRemote<void(RemoteData*)> delegateRemote(LambdaFuncRemoteDataPtr, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        RemoteData d(TEST_INT, TEST_INT + 1);
        delegateRemote(&d);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }
}

static void DelegateFunctionRemoteTests()
{
    using Del = DelegateFunctionRemote<void(int)>;

    Del delegate1(LambdaNoCapture, REMOTE_ID);
    delegate1.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    delegate1(TEST_INT);
    ASSERT_TRUE(delegate1.GetError() == DelegateError::ERR_NO_SERIALIZER);
    std::invoke(delegate1, TEST_INT);
    ASSERT_TRUE(delegate1.GetError() == DelegateError::ERR_NO_SERIALIZER);

    auto delegate2 = delegate1;
    ASSERT_TRUE(delegate1 == delegate2);
    ASSERT_TRUE(!delegate1.Empty());
    ASSERT_TRUE(!delegate2.Empty());

    Del delegate3(LambdaNoCapture, REMOTE_ID);
    delegate3 = delegate1;
    ASSERT_TRUE(delegate3 == delegate1);
    ASSERT_TRUE(delegate3);

    delegate3.Clear();
    ASSERT_TRUE(delegate3.Empty());
    ASSERT_TRUE(!delegate3);

    auto* delegate4 = delegate1.Clone();
    ASSERT_TRUE(*delegate4 == delegate1);
    delete delegate4;

    auto delegate5 = std::move(delegate1);
    ASSERT_TRUE(!delegate5.Empty());
    ASSERT_TRUE(delegate1.Empty());

    Del delegate6(LambdaNoCapture, REMOTE_ID);
    delegate6 = std::move(delegate2);
    ASSERT_TRUE(!delegate6.Empty());
    ASSERT_TRUE(delegate2.Empty());
    ASSERT_TRUE(delegate6 != nullptr);
    ASSERT_TRUE(nullptr != delegate6);
    ASSERT_TRUE(delegate2 == nullptr);
    ASSERT_TRUE(nullptr == delegate2);

    // Compare disparate delegate types
    DelegateFree<void(int)> other;
    ASSERT_TRUE(!(delegate6.Equal(other)));

    delegate6 = nullptr;
    ASSERT_TRUE(delegate6.Empty());
    ASSERT_TRUE(delegate6 == nullptr);

    // Make sure we get a default constructed return value.
    TestReturn::val = 0;
    DelegateFunction<TestReturn()> testRet;
    ASSERT_TRUE(TestReturn::val == 0);
    testRet();
    ASSERT_TRUE(TestReturn::val == 1);

    auto c2 = std::make_shared<Class>();
    DelegateFunctionRemote<std::unique_ptr<int>(int)> delUnique;
    delUnique.SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
    auto tmp = delUnique(10);
    ASSERT_TRUE(tmp == nullptr);
    delUnique.Bind(LambdaUnqiue, REMOTE_ID);
    std::unique_ptr<int> up = delUnique(12);
    ASSERT_TRUE(up == nullptr);  // Remote delegate has default return value 

    auto delS1 = MakeDelegate(LambdaNoCapture, REMOTE_ID);
    auto delS2 = MakeDelegate(LambdaNoCapture2, REMOTE_ID);
    //ASSERT_TRUE(!(delS1 == delS2));  // std::function can't distiguish difference

    std::set<Del> setDel;
    setDel.insert(delS1);
    setDel.insert(delS2);
    ASSERT_TRUE(setDel.size() == 1);

    delS1.Clear();
    ASSERT_TRUE(delS1.Empty());
    std::swap(delS1, delS2);
    ASSERT_TRUE(!delS1.Empty());
    ASSERT_TRUE(delS2.Empty());

    // Array of delegates
    Del* arr = new Del[2];
    arr[0].Bind(LambdaNoCapture, REMOTE_ID);
    arr[1].Bind(LambdaNoCapture, REMOTE_ID);
    for (int i = 0; i < 2; i++) {
        arr[i].SetErrorHandler(MakeDelegate([](DelegateRemoteId, DelegateError, int) {}));
        arr[i](TEST_INT);
    }
    delete[] arr;

    std::function NoError = [](std::shared_ptr<const StructParam> c) {};
    std::function NoError2 = [](const std::string& s, std::shared_ptr<const StructParam> c) {};
    std::function Error = [](std::shared_ptr<const StructParam>& c) {};
    std::function Error2 = [](const std::shared_ptr<const StructParam>& c) {};
    std::function Error3 = [](std::shared_ptr<const StructParam>* c) {};
    std::function Error4 = [](const std::shared_ptr<const StructParam>* c) {};

    auto noErrorDel = MakeDelegate(NoError, REMOTE_ID);
    auto noErrorDel2 = MakeDelegate(NoError2, REMOTE_ID);
#if 0
    // Causes compiler error. shared_ptr references not allowed; undefined behavior
    // in multi-threaded system.
    auto errorDel = MakeDelegate(Error, REMOTE_ID);
    auto errorDel2 = MakeDelegate(Error2, REMOTE_ID);
    auto errorDel3 = MakeDelegate(Error3, REMOTE_ID);
    auto errorDel4 = MakeDelegate(Error4, REMOTE_ID);
#endif

    // MakeDelegate with raw lambda — no std::function wrapper required
    {
        // Inline raw lambda
        auto d1 = MakeDelegate([](int i) {}, REMOTE_ID);
        ASSERT_TRUE(!d1.Empty());
        ASSERT_TRUE(d1.GetRemoteId() == REMOTE_ID);

        // Named raw lambda (auto, not std::function)
        auto rawLam = [](int i) {};
        auto d2 = MakeDelegate(rawLam, REMOTE_ID);
        ASSERT_TRUE(!d2.Empty());
        ASSERT_TRUE(d2.GetRemoteId() == REMOTE_ID);

        // Capturing lambda
        int captured = 0;
        auto d3 = MakeDelegate([captured](int i) {}, REMOTE_ID);
        ASSERT_TRUE(!d3.Empty());
        ASSERT_TRUE(d3.GetRemoteId() == REMOTE_ID);
    }

    std::function<void(DelegateRemoteId, DelegateError, DelegateErrorAux)> errorHandler = [](DelegateRemoteId id, DelegateError error, DelegateErrorAux code) {
        if (error != dmq::DelegateError::SUCCESS)
            ASSERT_TRUE(false);
    };
    std::function<void(DelegateRemoteId, DelegateError, int)> errorHandlerNoAssert = [](DelegateRemoteId id, DelegateError error, DelegateErrorAux code) {};
    MockDispatcher dispatcher;
    xostringstream os(ios::in | ios::out | ios::binary);
    auto remoteClass = std::make_shared<RemoteClass>();

    {
        Remote::Serializer<void(int)> serializer;
        DelegateMemberRemote<RemoteClass, void(int)> delegateRemote(remoteClass, &RemoteClass::MemberFuncInt, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        delegateRemote(TEST_INT);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::SUCCESS);

        delegateRemote.SetErrorHandler(MakeDelegate(errorHandlerNoAssert));
        recv_stream.setstate(std::ios::failbit);
        delegateRemote.Invoke(recv_stream);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::ERR_STREAM_NOT_GOOD);
        ASSERT_TRUE(delegateRemote.GetError() == DelegateError::SUCCESS);
    }

    {
        Remote::Serializer<void(int&)> serializer;
        DelegateMemberRemote<RemoteClass, void(int&)> delegateRemote(remoteClass, &RemoteClass::MmberFuncIntRef, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetStream(&os);
        delegateRemote.SetSerializer(&serializer);
        int i = TEST_INT;
        delegateRemote(i);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

    {
        Remote::Serializer<void(int*)> serializer;
        DelegateMemberRemote<RemoteClass, void(int*)> delegateRemote(remoteClass, &RemoteClass::MemberFuncIntPtr, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        int i = TEST_INT;
        delegateRemote(&i);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

    {
        Remote::Serializer<void(RemoteData&)> serializer;
        DelegateMemberRemote<RemoteClass, void(RemoteData&)> delegateRemote(remoteClass, &RemoteClass::MemberFuncRemoteData, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        RemoteData d(TEST_INT, TEST_INT + 1);
        delegateRemote(d);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }

    {
        Remote::Serializer<void(RemoteData*)> serializer;
        DelegateMemberRemote<RemoteClass, void(RemoteData*)> delegateRemote(remoteClass, &RemoteClass::MemberFuncRemoteDataPtr, REMOTE_ID);
        delegateRemote.SetErrorHandler(MakeDelegate(errorHandler));
        delegateRemote.SetStream(&os);
        delegateRemote.SetDispatcher(&dispatcher);
        delegateRemote.SetSerializer(&serializer);
        RemoteData d(TEST_INT, TEST_INT + 1);
        delegateRemote(&d);

        std::istream& recv_stream = dispatcher.GetDispached();
        recv_stream.seekg(0);
        delegateRemote.Invoke(recv_stream);
    }
}

void DelegateRemoteTests()
{
    DelegateFreeRemoteTests();
    DelegateMemberRemoteTests();
    DelegateMemberSpRemoteTests();
    DelegateFunctionRemoteTests();
}
