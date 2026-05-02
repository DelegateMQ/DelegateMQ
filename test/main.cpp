// @see https://github.com/DelegateMQ/DelegateMQ
// David Lafreniere, 2025.

/// @file main.cpp
/// @brief Main entry point for the DelegateMQ examples and unit tests.
///
/// @details
/// This file orchestrates the execution of a wide variety of usage examples and design 
/// patterns implemented using the DelegateMQ library. It establishes the necessary 
/// threading infrastructure (Worker Threads, Timer Loop) and invokes demonstration 
/// functions for:
///
/// **Core Functionality:**
/// * Synchronous vs. Asynchronous delegate invocation.
/// * Remote function calls (IPC/RPC) via `RemoteCommunication`.
/// * Signaling using `Signal` (thread-safe, direct member).
/// * Asynchronous Futures and Lambdas.
///
/// **Design Patterns:**
/// * Active Object & Async Method Invocation (AMI).
/// * Publisher/Subscriber & Observer.
/// * Command, Reactor, Proactor, and Producer-Consumer.
///
/// **Configuration:**
/// * To run performance/load tests, uncomment `#define STRESS_TEST` or `#define STRESS_TEST_REMOTE`.
///
/// See README.md for Library Overview.
/// See DETAILS.md for Porting Guides and Design Documentation.

#include "DelegateMQ.h"
#include "SafeTimer.h"
#include "AsyncCallback.h"
#include "AsyncWorker.h"
#include "SignalSlot.h"
#include "SignalSlotNoHeap.h"
#include "ProducerConsumer.h"
#include "CountdownLatch.h"
#include "ActiveObject.h"
#include "AsyncMethodInvocation.h"
#include "AsyncFuture.h"
#include "AsyncAPI.h"
#include "AllTargets.h"
#include "Observer.h"
#include "Reactor.h"
#include "Proactor.h"
#include "Command.h"
#include "BindingProperty.h"
#include "RemoteCommunication.h"
#include "DataBus.h"
#include "main.h"

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace dmq::serialization::serializer;
using namespace dmq::transport;

extern void RunDelegateUnitTests();
#if defined(DMQ_DATABUS)
extern void RunDataBusTests();
#endif
void RunSimpleExamples();
void RunRemoteChannelExamples();
void RunPubSubExamples();
void RunAsyncAPIExamples();
void RunAllExamples();
void RunMiscExamples();

using namespace Main;

std::atomic<bool> processTimerExit = false;

void ProcessTimers()
{
    while (!processTimerExit.load())
    {
        // @TODO: Must periodically call ProcessTimers for timer operation.
        // Process all delegate-based timers
        Timer::ProcessTimers();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

Timer& GetTimer()
{
    static Timer instance;
    return instance;
}

//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------
int main(void)
{
// Uncomment one to run the DelegateMQ stress tests. **Release** builds run faster!
//#define STRESS_TEST
//#define STRESS_TEST_REMOTE

#ifdef STRESS_TEST
    extern int stress_test();
    stress_test();
    return 0;
#endif

#ifdef STRESS_TEST_REMOTE
    extern int stress_test_remote();
    stress_test_remote();
    return 0;
#endif

    StartLogger();

    // Create a worker thread with a 5s watchdog timeout
    workerThread1.CreateThread(std::chrono::milliseconds(5000));
    SysDataNoLock::GetInstance();

    // Create a timer that expires every 1S and calls
    // TimerExpiredCb on workerThread1 upon expiration
    dmq::ScopedConnection timerConn = GetTimer().OnExpired.Connect(MakeTimerDelegate(&TimerExpiredCb, workerThread1));
    GetTimer().Start(std::chrono::seconds(1));

    // Start the thread that will run ProcessTimers
    std::thread timerThread(ProcessTimers);

    // Run all test code
    for (int i = 0; i < 3; i++)
    {
        RunSimpleExamples();
        RunRemoteChannelExamples();
        RunPubSubExamples();
        RunAsyncAPIExamples();
        RunAllExamples();
        RunMiscExamples();
        RunDelegateUnitTests();
#if defined(DMQ_DATABUS)
        RunDataBusTests();
#endif
    }

    // Ensure the timer thread completes before main exits
    processTimerExit.store(true);
    if (timerThread.joinable())
        timerThread.join();

    GetTimer().Stop();

    workerThread1.ExitThread();

    cout << "Success!" << endl;
    return 0;
}

size_t MsgOut(const std::string& msg)
{
    std::cout << "[" << std::this_thread::get_id() << "] " << msg << std::endl;
    return msg.size();
}

//------------------------------------------------------------------------------
// Examples to create and invoke sync, async and remote delegates
//------------------------------------------------------------------------------
void RunSimpleExamples()
{
    class MockTransport : public dmq::transport::ITransport
    {
    public:
        int Send(dmq::xostringstream& os, const dmq::transport::DmqHeader& header) override {
            // @TODO: Send serialized data to the remote endpoint.
            // See example\sample-projects\system-architecture-no-deps
            // for a complete working remote delegate client/server example.
            cout << "Transport Send DelegateRemoteId=" << header.GetId() << endl;
            return 0;
        }
        int Receive(dmq::xstringstream& is, dmq::transport::DmqHeader& header) override { return 0; }
    };

    // 1. Synchronous Invocation
    auto sync = dmq::MakeDelegate(&MsgOut);
    sync("Invoke MsgOut sync!");

    // 2. Asynchronous Invocation (Non-blocking / Fire-and-forget)
    auto async = dmq::MakeDelegate(&MsgOut, workerThread1);
    async("Invoke MsgOut async (non-blocking)!");

    // 3. Asynchronous Invocation (Blocking / Wait for result)
    auto asyncWait = dmq::MakeDelegate(&MsgOut, workerThread1, dmq::WAIT_INFINITE);
    size_t size = asyncWait("Invoke MsgOut async wait (blocking)!");

    // 4. Asynchronous Invocation with Timeout
    auto asyncWait1s = dmq::MakeDelegate(&MsgOut, workerThread1, std::chrono::seconds(1));
    auto retVal = asyncWait1s.AsyncInvoke("Invoke MsgOut async wait (blocking max 1s)!");
    if (retVal.has_value())     // Async invoke completed within 1 second?
        size = retVal.value();  // Get return value

    // 5. Remote Invocation — RemoteChannel owns transport wiring (dispatcher, stream, serializer)
    MockTransport transport;
    dmq::serialization::serializer::Serializer<void(std::string)> serializer;
    dmq::RemoteChannel<void(std::string)> channel(transport, serializer);
    channel.Bind(std::function<void(std::string)>(&MsgOut), dmq::DelegateRemoteId(1));
    channel("Invoke MsgOut remote!");
}

//------------------------------------------------------------------------------
// MsgSender / MsgReceiver remote delegate example (in-process loopback)
//
// LoopbackTransport bridges sender and receiver in-process: Send() immediately
// delivers the serialized payload to the registered receiver's Invoke(),
// simulating a round-trip without a real transport.
//------------------------------------------------------------------------------

// Loopback transport: on Send(), deserializes and invokes the registered receiver
class LoopbackTransport : public dmq::transport::ITransport
{
public:
    void SetReceiver(dmq::IRemoteInvoker* recv) { m_receiver = recv; }

    int Send(dmq::xostringstream& os, const dmq::transport::DmqHeader& header) override {
        cout << "LoopbackTransport Send DelegateRemoteId=" << header.GetId() << endl;
        if (m_receiver) {
            dmq::xstringstream is(os.str());
            m_receiver->Invoke(is);
        }
        return 0;
    }
    int Receive(dmq::xstringstream& is, dmq::transport::DmqHeader& header) override { return 0; }

private:
    dmq::IRemoteInvoker* m_receiver = nullptr;
};

constexpr dmq::DelegateRemoteId MSG_OUT_ID = 10;

// Receiver: binds MsgOut as the remote handler
class MsgReceiver
{
public:
    MsgReceiver(dmq::transport::ITransport& transport, dmq::ISerializer<void(std::string)>& ser)
        : m_channel(transport, ser)
    {
        m_channel.Bind(std::function<void(std::string)>(&MsgOut), MSG_OUT_ID);
    }

    dmq::IRemoteInvoker* GetEndpoint() { return m_channel.GetEndpoint(); }

private:
    dmq::RemoteChannel<void(std::string)> m_channel;
};

// Sender: invokes MsgOut on the remote receiver
class MsgSender
{
public:
    MsgSender(dmq::transport::ITransport& transport, dmq::ISerializer<void(std::string)>& ser)
        : m_channel(transport, ser)
    {
        m_channel.Bind(std::function<void(std::string)>([](std::string) {}), MSG_OUT_ID);
    }

    void Send(const std::string& msg) { m_channel(msg); }

private:
    dmq::RemoteChannel<void(std::string)> m_channel;
};

void RunRemoteChannelExamples()
{
    dmq::serialization::serializer::Serializer<void(std::string)> serializer;
    LoopbackTransport transport;

    MsgReceiver receiver(transport, serializer);
    transport.SetReceiver(receiver.GetEndpoint());

    MsgSender sender(transport, serializer);

    // Sender serializes args -> LoopbackTransport::Send() -> MsgReceiver::Invoke() -> MsgOut()
    sender.Send("MsgSender -> MsgReceiver via loopback!");
}

// Signal<> is inherently thread-safe: subscribers dispatch onto their own
// threads, and Disconnect() may be called safely from any thread.
class Publisher
{
public:
    // 1. Define a thread-safe OnMessage signal as a direct member.
    dmq::Signal<void(const std::string&)> OnMessage;

    void Publish(const std::string& msg)
    {
        // 3. Emit signal to all connected slots
        OnMessage(msg);
    }
};

class Subscriber
{
public:
    Subscriber(Publisher& pub) : m_thread("SubscriberThread")
    {
        m_thread.CreateThread();

        // 2. Connect to the publisher's signal
        m_connection = pub.OnMessage.Connect(
            dmq::MakeDelegate(this, &Subscriber::HandleMsg, m_thread)
        );
    }

    // 5. Destructor automatically disconnects m_connection via RAII
    ~Subscriber() = default;

private:
    void HandleMsg(const std::string& msg)
    {
        // 4. Handle publisher signal on m_thread
        std::cout << "Writing data on thread: " << Thread::GetCurrentThreadId() << std::endl;
        std::cout << msg << std::endl;
    }

    Thread m_thread;
    dmq::ScopedConnection m_connection; // Automatically disconnects when Subscriber is destroyed
};

//------------------------------------------------------------------------------
// Run pub/sub example
//------------------------------------------------------------------------------
void RunPubSubExamples()
{
    Publisher pub;

    {
        Subscriber sub(pub);
        pub.Publish("Hello World!");
    } // sub goes out of scope here -> m_connection disconnects automatically

    pub.Publish("No one is listening..."); // Safe, no slots connected
}

class Data
{
public:
    int x = 0;
};

// Store data using asynchronous public API. Class is thread-safe.
class DataStore : public std::enable_shared_from_this<DataStore>
{
public:
    DataStore() : m_thread("DataStoreThread")
    {
        m_thread.CreateThread();
    }

    // Store data asynchronously on m_thread context (non-blocking)
    void StoreAsync(const Data& data)
    {
        // If the caller thread is not the internal thread, reinvoke this function 
        // asynchronously on the internal thread to ensure thread-safety
        if (!m_thread.IsCurrentThread())
        {
            // Reinvoke StoreAsync(data) on m_thread context
            return dmq::MakeDelegate(shared_from_this(), &DataStore::StoreAsync, m_thread)(data);
        }
        std::cout << "Writing data on thread: " << Thread::GetCurrentThreadId() << std::endl;
        m_data = data;  // Data stored on m_thread context
    }

    // Alternate approach using AsyncInvoke helper function (blocking with 
    // return value)
    bool StoreAsync2(const Data& data)
    {
        // Capture 'self' (shared_ptr) instead of raw 'this'
        auto self = shared_from_this();

        auto storeLambda = [self](const Data& data) -> bool {
            // This runs on m_thread
            std::cout << "Writing data on thread: " << Thread::GetCurrentThreadId() << std::endl;
            self->m_data = data;
            return true;
            };

        return AsyncInvoke(storeLambda, m_thread, WAIT_INFINITE, data);
    }

private:
    Data m_data;        // Data storage
    Thread m_thread;    // Internal thread
};

//------------------------------------------------------------------------------
// Asynchronous API examples
//------------------------------------------------------------------------------
void RunAsyncAPIExamples()
{
    // We use shared_ptr to ensure the object remains alive for the duration of the
    // asynchronous call. If we allocated DataStore on the stack, it would be destroyed 
    // immediately when this function returns. The worker thread would then attempt 
    // to access a dead object (Use-After-Free), resulting in a crash.
    // See DETAILS.md section Object Lifetime and Async Delegates.
    auto dataStore = std::make_shared<DataStore>();

    Data d;
    d.x = 123;

    // Invoke async API functions
    dataStore->StoreAsync(d);
    bool rv2 = dataStore->StoreAsync2(d);
}

//------------------------------------------------------------------------------
// Run all example modules
//------------------------------------------------------------------------------
void RunAllExamples()
{
    using namespace Example;

    // Run all target types example
    AllTargetsExample();

    // Run safe timer example
    SafeTimerExample();

    // Run asynchronous callbacks using delegates example
    AsyncCallbackExample();

    // Run signal-slot example
    RunSignalSlotExamples();

    // Signal as a plain member variable (single-threaded, minimal heap)
    SignalSlotMemberExample();

    // Signal slot usage without heap
    SignalSlotNoHeapExample();

    // Run asynchronous API using delegates example 
    AsyncAPIExample();

    // Run the asychronous invocation example (AMI) example
    AsyncMethodInvocationExample();

    // Run the std::async and std::future example with delegates
    AsyncFutureExample();

    // Run async worker thread example
    AsyncWorkerExample();

    // Run the active object pattern example
    ActiveObjectExample();

    // Run the binding property pattern example
    BindingPropertyExample();

    // Run the command pattern example
    CommandExample();

    // Run observer pattern example
    ObserverExample();

    // Run producer-consumer pattern example
    ProducerConsumerExample();

    // Run reactor pattern example
    ReactorExample();

    // Run proactor pattern example
    ProactorExample();

    // Run remote communication example
    RemoteCommunicationExample();

    // Run DataBus example
    DataBusExample();

    // C++20 examples
#if defined(_MSVC_LANG) && _MSVC_LANG >= 202002L || __cplusplus >= 202002L
    // Run countdown latch example
    CountdownLatchExample();
#endif
}

//------------------------------------------------------------------------------
// Run misc delegate examples
//------------------------------------------------------------------------------
TestClass testClass;
void RunMiscExamples()
{
    // Create a worker thread with a 5s watchdog timeout
    workerThread1.CreateThread(std::chrono::milliseconds(5000));
    SysDataNoLock::GetInstance();

    TestStruct testStruct;
    testStruct.x = 123;
    TestStruct* pTestStruct = &testStruct;

    // Create a delegate bound to a free function then invoke
    auto delegateFree = MakeDelegate(&FreeFuncInt);
    delegateFree(123);

    // Create a delegate bound to a member function then invoke
    auto delegateMember = MakeDelegate(&testClass, &TestClass::MemberFunc);
    delegateMember(&testStruct);

    // Create a delegate bound to a member function. Assign and invoke from
    // a base reference. 
    auto delegateMember2 = MakeDelegate(&testClass, &TestClass::MemberFunc);
    delegateMember2(&testStruct);

    // Create a multicast delegate container that accepts Delegate<void(int)> delegates.
    // Any function with the signature "void Func(int)".
    MulticastDelegate<void(int)> delegateA;

    // Add a DelegateFree1<int> delegate to the container 
    delegateA += MakeDelegate(&FreeFuncInt);

    // Invoke the delegate target free function FreeFuncInt()
    if (delegateA)
        delegateA(123);

    // Remove the delegate from the container
    delegateA -= MakeDelegate(&FreeFuncInt);

    // Create a multicast delegate container that accepts Delegate<void (TestStruct*)> delegates
    // Any function with the signature "void Func(TestStruct*)".
    MulticastDelegate<void(TestStruct*)> delegateB;

    // Add a DelegateMember<TestStruct*> delegate to the container
    delegateB += MakeDelegate(&testClass, &TestClass::MemberFunc);

    // Invoke the delegate target member function TestClass::MemberFunc()
    if (delegateB)
        delegateB(&testStruct);

    // Remove the delegate from the container
    delegateB -= MakeDelegate(&testClass, &TestClass::MemberFunc);

    // Create a thread-safe multicast delegate container that accepts Delegate<void (TestStruct*)> delegates
    // Any function with the signature "void Func(TestStruct*)".
    MulticastDelegateSafe<void(TestStruct*)> delegateC;

    // Add a DelegateMember<TestStruct*> delegate to the container that will invoke on workerThread1
    delegateC += MakeDelegate(&testClass, &TestClass::MemberFunc, workerThread1);

    // Asynchronously invoke the delegate target member function TestClass::MemberFunc()
    if (delegateC)
        delegateC(&testStruct);

    // Remove the delegate from the container
    delegateC -= MakeDelegate(&testClass, &TestClass::MemberFunc, workerThread1);

    // Create a thread-safe multicast delegate container that accepts Delegate<void (TestStruct&, float, int**)> delegates
    // Any function with the signature "void Func(const TestStruct&, float, int**)".
    MulticastDelegateSafe<void(const TestStruct&, float, int**)> delegateD;

    // Add a delegate to the container that will invoke on workerThread1
    delegateD += MakeDelegate(&testClass, &TestClass::MemberFuncThreeArgs, workerThread1);

    // Asynchronously invoke the delegate target member function TestClass::MemberFuncThreeArgs()
    if (delegateD)
    {
        int i = 555;
        int* pI = &i;
        delegateD(testStruct, 1.23f, &pI);
    }

    // Remove the delegate from the container
    delegateD -= MakeDelegate(&testClass, &TestClass::MemberFuncThreeArgs, workerThread1);

    // Create a unicast delegate container that accepts Delegate<int (int)> delegates.
    // Any function with the signature "int Func(int)".
    UnicastDelegate<int(int)> delegateF;

    // Add a DelegateFree<int(int)> delegate to the container 
    delegateF = MakeDelegate(&FreeFuncIntRetInt);

    // Invoke the delegate target free function FreeFuncInt()
    int retVal = 0;
    if (delegateF)
        retVal = delegateF(123);

    // Remove the delegate from the container
    delegateF.Clear();

    // Create a unicast delegate container that accepts delegates with 
    // the signature "void Func(TestStruct**)"
    UnicastDelegate<void(TestStruct**)> delegateG;

    // Make a delegate that points to a free function 
    delegateG = MakeDelegate(&FreeFuncPtrPtrTestStruct);

    // Invoke the delegate target function FreeFuncPtrPtrTestStruct()
    delegateG(&pTestStruct);

    // Remove the delegate from the container
    delegateG = 0;

    // Create delegate with std::string and int arguments then asynchronously 
    // invoke on a member function
    MulticastDelegateSafe<void(const std::string&, int)> delegateH;
    delegateH += MakeDelegate(&testClass, &TestClass::MemberFuncStdString, workerThread1);
    delegateH("Hello world", 2020);
    delegateH.Clear();

    // Create a asynchronous blocking delegate and invoke. This thread will block until the 
    // msg and year stack values are set by MemberFuncStdStringRetInt on workerThread1.
    auto delegateI = MakeDelegate(&testClass, &TestClass::MemberFuncStdStringRetInt, workerThread1, WAIT_INFINITE);
    std::string msg;
    int year = delegateI(msg);
    if (delegateI.IsSuccess())
    {
        auto year2 = delegateI.GetRetVal();
        cout << msg.c_str() << " " << year << endl;
    }

    // Alternate means to invoke a function asynchronousy using AsyncInvoke. This thread will block until the 
    // msg and year stack values are set by MemberFuncStdStringRetInt on workerThread1.
    std::string msg2;
    auto asyncInvokeRetVal = MakeDelegate(&testClass, &TestClass::MemberFuncStdStringRetInt, workerThread1, std::chrono::milliseconds(100)).AsyncInvoke(msg2);
    if (asyncInvokeRetVal.has_value())
        cout << msg.c_str() << " " << asyncInvokeRetVal.value() << endl;
    else
        cout << "Asynchronous call to MemberFuncStdStringRetInt failed to invoke within specified timeout!";

    // Invoke function asynchronously non-blocking. Does NOT wait for return value; Return is default value. 
    MakeDelegate(&testClass, &TestClass::TestFuncUserTypeRet, workerThread1).AsyncInvoke();

    // Invoke function asynchronously blocking with user defined return type
    auto testStructRet2 = MakeDelegate(&testClass, &TestClass::TestFuncUserTypeRet, workerThread1, WAIT_INFINITE).AsyncInvoke();

    // Invoke functions asynchronously blocking with no return value
    auto noRetValRet = MakeDelegate(&testClass, &TestClass::TestFuncNoRet, workerThread1, std::chrono::milliseconds(10)).AsyncInvoke();
    auto noRetValRet2 = MakeDelegate(&FreeFuncInt, workerThread1, milliseconds(10)).AsyncInvoke(123);
    if (noRetValRet.has_value() && noRetValRet2.has_value())
        cout << "Asynchronous calls with no return value succeeded!" << endl;

    // Create a shared_ptr, create a delegate, then synchronously invoke delegate function
    std::shared_ptr<TestClass> spObject(new TestClass());
    auto delegateMemberSp = MakeDelegate(spObject, &TestClass::MemberFuncStdString);
    delegateMemberSp("Hello world using shared_ptr", 2020);

    // Example of a bug where the testClassHeap is deleted before the asynchronous delegate 
    // is invoked on the workerThread1. In other words, by the time workerThread1 calls
    // the bound delegate function the testClassHeap instance is deleted and no longer valid.
#if 0
    TestClass* testClassHeap = new TestClass();
    auto delegateMemberAsync = MakeDelegate(testClassHeap, &TestClass::MemberFuncStdString, workerThread1);
    delegateMemberAsync("Function async invoked on deleted object. Bug!", 2020);
    delegateMemberAsync.Clear();
    delete testClassHeap;
#endif

    // Example of the smart pointer function version of the delegate. The testClassSp instance 
    // is only deleted after workerThread1 invokes the callback function thus solving the bug.
    std::shared_ptr<TestClass> testClassSp(new TestClass());
    auto delegateMemberSpAsync = MakeDelegate(testClassSp, &TestClass::MemberFuncStdString, workerThread1);
    delegateMemberSpAsync("Function async invoked using smart pointer. Bug solved!", 2020);
    delegateMemberSpAsync.Clear();
    testClassSp.reset();

    {
        // Example of a shared_ptr argument that does not copy the function
        // argument data. 
        auto delegateJ = MakeDelegate(&testClass, &TestClass::MemberFuncNoCopy, workerThread1);
        std::shared_ptr<TestStructNoCopy> testStructNoCopy = std::make_shared<TestStructNoCopy>(987);
        delegateJ(testStructNoCopy);
    }

    // Example of using std::shared_ptr function arguments with asynchrononous delegate. Using a 
    // shared_ptr<T> argument ensures that the argument T is not copied for each registered client.
    // Could be helpful if T is very large and two or more clients register to receive asynchronous
    // callbacks.
    CoordinatesHandler coordinatesHandler;

    auto coordDelegate = MakeDelegate(&CoordinatesChangedCallback, workerThread1);
    CoordinatesHandler::CoordinatesChanged += coordDelegate;

    Coordinates coordinates;
    coordinates.x = 11;
    coordinates.y = 99;
    coordinatesHandler.SetData(coordinates);

    CoordinatesHandler::CoordinatesChanged -= coordDelegate;

    // Invoke virtual base function example
    std::shared_ptr<Base> base = std::make_shared<Derived>();
    auto baseDelegate = MakeDelegate(base, &Base::display, workerThread1);
    baseDelegate("Invoke Derviced::display()!");

    // Pass delegate argument examples
    auto delegateFreeSync = MakeDelegate(&FreeFuncInt);
    FreeFuncDelegateArg(delegateFreeSync);
    auto delegateFreeAsync = MakeDelegate(&FreeFuncInt, workerThread1);
    FreeFuncDelegateArg(delegateFreeAsync);
    auto delegateFreeAsyncWait = MakeDelegate(&FreeFuncInt, workerThread1, WAIT_INFINITE);
    FreeFuncDelegateArg(delegateFreeAsyncWait);

    // Begin lambda examples. Lambda captures not allowed if delegates used to invoke.
    DelegateFunction<int(int)> delFunc([](int x) -> int { return x + 5; });
    int val = delFunc(8);

    std::function LambdaFunc1 = [](int i) -> int
        {
            cout << "Called LambdaFunc1 " << i << std::endl;
            return ++i;
        };

    // Asynchronously invoke lambda on workerThread1 and wait for the return value
    auto lambdaDelegate1 = MakeDelegate(LambdaFunc1, workerThread1, WAIT_INFINITE);
    int lambdaRetVal2 = lambdaDelegate1(123);

    auto LambdaFunc2 = +[](const TestStruct& s, bool b)
        {
            cout << "Called LambdaFunc2 " << s.x << " " << b << std::endl;
        };

    // Invoke lambda via function pointer without delegates
    int lambdaRetVal1 = LambdaFunc1(876);

    TestStruct lambdaArg;
    lambdaArg.x = 4321;

    // Asynchronously invoke lambda on workerThread1 without waiting
    auto lambdaDelegate2 = MakeDelegate(LambdaFunc2, workerThread1);
    lambdaDelegate2(lambdaArg, true);

    // Asynchronously invoke lambda on workerThread1 using AsyncInvoke
    auto lambdaRet = MakeDelegate(LambdaFunc1, workerThread1, std::chrono::milliseconds(100)).AsyncInvoke(543);
    if (lambdaRet.has_value())
        cout << "LambdaFunc1 success! " << lambdaRet.value() << endl;

    std::vector<int> v{ 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    // Synchronous lambda example
    const auto valResult = std::count_if(v.begin(), v.end(),
        [](int v) { return v > 2 && v <= 6; });
    cout << "Synchronous lambda result: " << valResult << endl;

    // Asynchronous lambda example (pass delegate to algorithm)
    auto CountLambda = +[](int v) -> int
        {
            return v > 2 && v <= 6;
        };
    auto countLambdaDelegate = MakeDelegate(CountLambda, workerThread1, WAIT_INFINITE);

    // Alternate syntax
    //auto countLambdaDelegate = MakeDelegate(
    //    +[](int v) { return v > 2 && v <= 6; },
    //	workerThread1, 
    //	WAIT_INFINITE);

    const auto valAsyncResult = std::count_if(v.begin(), v.end(),
        countLambdaDelegate);
    cout << "Asynchronous lambda result: " << valAsyncResult << endl;

    // Long running function 
    std::function AddFunc = [](int a, int b) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return a + b;
        };

    // Create async delegate with lambda target function
    auto addDelegate = MakeDelegate(AddFunc, workerThread1, WAIT_INFINITE);

    // Using std::async, invokes AddFunc on workerThread1
    std::future<int> result = std::async(std::launch::async, addDelegate, 5, 3);

    cout << "Do work while waiting for AddFunc to complete." << endl;

    // Wait for AddFunc return value
    int sum = result.get();
    cout << "AddFunc return value: " << sum << " ";
    // End lambda examples

    // Example shows std::function target limitations. Not a normal usage case.
    // Use MakeDelegate() to create delegates works correctly with delegate 
    // containers.
    Test t1, t2;
    std::function<void(int)> f1 = std::bind(&Test::Func, &t1, std::placeholders::_1);
    std::function<void(int)> f2 = std::bind(&Test::Func2, &t2, std::placeholders::_1);
    MulticastDelegateSafe<void(int)> safe;
    safe += MakeDelegate(f1);
    safe += MakeDelegate(f2);
    safe -= MakeDelegate(f2);   // Should remove f2, not f1!

    // Example of safely creating a shared object that receives async callbacks.
    // SysDataClient will only be destroyed after all outstanding callbacks complete.
    auto client = std::make_shared<Main::SysDataClient>();
    client->Init();

    // Set new SystemMode values. Each call will invoke callbacks to all 
    // registered client subscribers.
    SysData::GetInstance().SetSystemMode(SystemMode::STARTING);
    SysData::GetInstance().SetSystemMode(SystemMode::NORMAL);

    // Set new SystemMode values for SysDataNoLock.
    SysDataNoLock::GetInstance().SetSystemMode(SystemMode::SERVICE);
    SysDataNoLock::GetInstance().SetSystemMode(SystemMode::SYS_INOP);

    // Set new SystemMode values for SysDataNoLock using async API
    SysDataNoLock::GetInstance().SetSystemModeAsyncAPI(SystemMode::SERVICE);
    SysDataNoLock::GetInstance().SetSystemModeAsyncAPI(SystemMode::SYS_INOP);

    // Set new SystemMode values for SysDataNoLock using async wait API
    SystemMode::Type previousMode;
    previousMode = SysDataNoLock::GetInstance().SetSystemModeAsyncWaitAPI(SystemMode::STARTING);
    previousMode = SysDataNoLock::GetInstance().SetSystemModeAsyncWaitAPI(SystemMode::NORMAL);

    client->Term();

    // RAII Connection Management Example (Signal/ScopedConnection)
    // Signal is a plain local variable — no shared_ptr required.
    // See SignalSlot.cpp for more examples.
    {
        dmq::Signal<void(int)> OnSignal;

        {
            // Create a scoped connection handle
            dmq::ScopedConnection conn;

            // Connect a lambda to the "OnSignal" event
            conn = OnSignal.Connect(MakeDelegate(+[](int v) {
                cout << "ScopedConnection received: " << v << endl;
                }));

            // Signal is connected
            cout << "Signal firing (Observer is alive):" << endl;
            OnSignal(100);
        } // conn goes out of scope here -> Automatically disconnects!

        // Signal is disconnected
        cout << "Signal firing (Observer is dead):" << endl;
        OnSignal(200); // No output expected
    }
}