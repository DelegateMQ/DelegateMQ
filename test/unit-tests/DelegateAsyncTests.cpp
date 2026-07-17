#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include <atomic>
#include <iostream>
#include <set>
#include <cstring>

using namespace dmq;
using namespace dmq::os;
using namespace std;
using namespace UnitTestData;

static Thread workerThread("DelegateAsyncTests");

namespace Async
{
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

    // Do not allow shared_ptr references. Causes compile error if used with Async delegates.
    void NoError(std::shared_ptr<const StructParam> c) {}
    void NoError2(const std::string& s, std::shared_ptr<const StructParam> c) {}
    void Error(std::shared_ptr<const StructParam>& c) {}
    void Error2(const std::shared_ptr<const StructParam>& c) {}
    void Error3(std::shared_ptr<const StructParam>* c) {}
    void Error4(const std::shared_ptr<const StructParam>* c) {}

    class ClassError
    {
    public:
        // Do not allow shared_ptr references. Causes compile error if used with Async delegates.
        void NoError(std::shared_ptr<const StructParam> c) {}
        void NoError2(const std::string& s, std::shared_ptr<const StructParam> c) {}
        void Error(std::shared_ptr<const StructParam>& c) {}
        void Error2(const std::shared_ptr<const StructParam>& c) {}
        void Error3(std::shared_ptr<const StructParam>* c) {}
        void Error4(const std::shared_ptr<const StructParam>* c) {}
    };
}
using namespace Async;

static void DelegateFreeAsyncTests()
{
    using Del = DelegateFreeAsync<void(int)>;

    Del delegate1(FreeFuncInt1, workerThread);
    delegate1(TEST_INT);
    std::invoke(delegate1, TEST_INT);

    delegate1.SetPriority(Priority::HIGH);
    auto delegate2 = delegate1;
    ASSERT_TRUE(delegate1 == delegate2);
    ASSERT_TRUE(delegate2.GetPriority() == Priority::HIGH);
    ASSERT_TRUE(!delegate1.Empty());
    ASSERT_TRUE(!delegate2.Empty());

    Del delegate3(FreeFuncInt1, workerThread);
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
    ASSERT_TRUE(delegate5.GetPriority() == Priority::HIGH);
    ASSERT_TRUE(!delegate5.Empty());
    ASSERT_TRUE(delegate1.Empty());

    Del delegate6(FreeFuncInt1, workerThread);
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
    DelegateFreeAsync<TestReturn()> testRet;
    ASSERT_TRUE(TestReturn::val == 0);
    testRet();
    ASSERT_TRUE(TestReturn::val == 1);

    DelegateFreeAsync<std::unique_ptr<int>(int)> delUnique;
    auto tmp = delUnique(10);
    ASSERT_TRUE(tmp == nullptr);
    delUnique.Bind(&FuncUnique, workerThread);
    std::unique_ptr<int> up = delUnique(12);
    ASSERT_TRUE(up == nullptr);  // Async delegate has default return value 

    auto delS1 = MakeDelegate(FreeFuncInt1, workerThread);
    auto delS2 = MakeDelegate(FreeFuncInt1_2, workerThread);
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

    std::function<int(int)> stdFunc = MakeDelegate(&FreeFuncIntWithReturn1, workerThread);
    int stdFuncRetVal = stdFunc(TEST_INT);
    ASSERT_TRUE(stdFuncRetVal == 0);

#if 0
    // ClassSingleton private constructor. Can't use singleton as ref (&),
    // pointer (*), or pointer-to-pointer (**) since async delegate makes 
    // copy of ClassSingleton argument.
    auto& singleton = ClassSingleton::GetInstance();
    auto delRef = MakeDelegate(&SetClassSingletonRef, workerThread);
    auto delPtr = MakeDelegate(&SetClassSingletonPtr, workerThread);
    auto delPtrPtr = MakeDelegate(&SetClassSingletonPtrPtr, workerThread);
#endif

    auto noErrorDel = MakeDelegate(&NoError, workerThread);
    auto noErrorDel2 = MakeDelegate(&NoError2, workerThread);
#if 0
    // Causes compiler error. shared_ptr references not allowed; undefined behavior
    // in multi-threaded system.
    auto errorDel = MakeDelegate(&Error, workerThread);
    auto errorDel2 = MakeDelegate(&Error2, workerThread);
    auto errorDel3 = MakeDelegate(&Error3, workerThread);
    auto errorDel4 = MakeDelegate(&Error4, workerThread);
#endif

    // Shared pointer does not copy singleton object; no copy of shared_ptr arg.
    auto singletonSp = ClassSingleton::GetInstanceSp();
    auto delShared = MakeDelegate(&SetClassSingletonShared, workerThread);
    delShared(singletonSp);

    // Invoke the type-safe delegates to verify dispatch doesn't crash at runtime.
    // shared_ptr arg is copied (the pointer, not the pointee); std::string arg is
    // deep-copied to the heap by the async dispatch mechanism.
    auto structParamSp = std::make_shared<const StructParam>();
    noErrorDel(structParamSp);
    noErrorDel2(std::string("hello world"), structParamSp);

    // Test nullptr arguments
    auto nullPtrArg = MakeDelegate(&NullPtrArg, workerThread);
    nullPtrArg(nullptr);
    auto nullPtrPtrArg = MakeDelegate(&NullPtrPtrArg, workerThread);
    nullPtrPtrArg(nullptr);

    // Test outgoing ptr argument
    // Target function does *not* change local instance data. A copy of delegate
    // and all arguments is sent to the destination thread.
    StructParam sparam;
    int iparam = 100;
    sparam.val = TEST_INT;
    auto outgoingArg = MakeDelegate(&OutgoingPtrArg, workerThread);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    outgoingArg(&sparam, &iparam);
    ASSERT_TRUE(sparam.val == TEST_INT);
    ASSERT_TRUE(iparam == 100);

    // Test outgoing ptr-ptr argument
    StructParam* psparam = nullptr;
    auto outgoingArg2 = MakeDelegate(&OutgoingPtrPtrArgAsync, workerThread);
    outgoingArg2(&psparam);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(psparam == nullptr);

    // Test outgoing ref argument
    sparam.val = TEST_INT;
    auto outgoingArg3 = MakeDelegate(&OutgoingRefArg, workerThread);
    outgoingArg3(sparam);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(sparam.val == TEST_INT);

    // Aync invoke copies Class object when passed to func
    Class classInstance;
    Class::m_construtorCnt = 0;
    auto cntDel = MakeDelegate(&ConstructorCnt, workerThread);
    cntDel(&classInstance);
    ASSERT_TRUE(Class::m_construtorCnt == 1);

    // Compile error. Invalid to pass void* argument to async target function
#if 0
    // Test void* args
    const char* str = "Hello World!";
    void* voidPtr = (void*)str;
    auto voidPtrNotNullDel = MakeDelegate(&VoidPtrArgNotNull, workerThread);
    voidPtrNotNullDel(voidPtr);
    auto voidPtrNullDel = MakeDelegate(&VoidPtrArgNull, workerThread);
    voidPtrNullDel(nullptr);
#endif

    // Test void* return. Return value is nullptr because return 
    // value is invalid on a non-blocking async delegate
    auto retVoidPtrDel = MakeDelegate(&RetVoidPtr, workerThread);
    auto retVoidPtr = retVoidPtrDel();
    ASSERT_TRUE(retVoidPtr == nullptr);
    const char* retStr = (const char*)retVoidPtr;
    ASSERT_TRUE(retStr == nullptr);

#if 0
    // Invalid: Can't pass a && argument through a message queue
    // Test rvalue ref
    auto rvalueRefDel = MakeDelegate(&FuncRvalueRef, workerThread);
    int rv = TEST_INT;
    rvalueRefDel(std::move(rv));
    rvalueRefDel(12345678);
#endif

    // Array of delegates
    Del* arr = new Del[2];
    arr[0].Bind(FreeFuncInt1, workerThread);
    arr[1].Bind(FreeFuncInt1, workerThread);
    for (int i = 0; i < 2; i++)
        arr[i](TEST_INT);
    delete[] arr;

    // Priority queue test
    std::atomic<int> sleepCnt = 0;
    std::atomic<bool> failed = false;
    std::atomic<bool> once = false;
    std::function<void(dmq::Priority)> LambdaSleep = [&sleepCnt, &failed, &once](dmq::Priority priority) {
        std::cout << "LambdaSleep: " << sleepCnt << ", priority: " << static_cast<int>(priority) << std::endl;
        if (!once)
        {
            sleepCnt++;
            once = true;

            // Let priority queue fill with other messages
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        else
        {
            // Check priority queue behavior
            if (priority == dmq::Priority::HIGH && (sleepCnt == 1 || sleepCnt == 2))
                sleepCnt++;
            else if (priority == dmq::Priority::NORMAL && (sleepCnt == 3 || sleepCnt == 4))
                sleepCnt++;
            else
                failed = true;  // Priority queue not working as expected
        }
        };
    auto delegateNormal = MakeDelegate(LambdaSleep, workerThread);
    delegateNormal.SetPriority(Priority::NORMAL);
    auto delegateHigh = MakeDelegate(LambdaSleep, workerThread);
    delegateHigh.SetPriority(Priority::HIGH);

    delegateHigh(dmq::Priority::HIGH);
    delegateNormal(dmq::Priority::NORMAL);
    delegateHigh(dmq::Priority::HIGH);
    delegateNormal(dmq::Priority::NORMAL);
    delegateHigh(dmq::Priority::HIGH);

    std::this_thread::sleep_for(std::chrono::seconds(3));
    ASSERT_TRUE(sleepCnt == 5);
    ASSERT_TRUE(failed == false);
}

static TestClass1 testClass1;
static void DelegateMemberAsyncTests()
{
    using Del = DelegateMemberAsync<TestClass1, void(int)>;

    Del delegate1(&testClass1, &TestClass1::MemberFuncInt1, workerThread);
    delegate1(TEST_INT);
    std::invoke(delegate1, TEST_INT);

    delegate1.SetPriority(Priority::HIGH);
    auto delegate2 = delegate1;
    ASSERT_TRUE(delegate1 == delegate2);
    ASSERT_TRUE(delegate2.GetPriority() == Priority::HIGH);
    ASSERT_TRUE(!delegate1.Empty());
    ASSERT_TRUE(!delegate2.Empty());

    Del delegate3(&testClass1, &TestClass1::MemberFuncInt1, workerThread);
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
    ASSERT_TRUE(delegate5.GetPriority() == Priority::HIGH);
    ASSERT_TRUE(!delegate5.Empty());
    ASSERT_TRUE(delegate1.Empty());

    Del delegate6(&testClass1, &TestClass1::MemberFuncInt1, workerThread);
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
    DelegateMemberAsync<const Class, std::uint16_t(void)> dConstClass;
    //dConstClass.Bind(&c, &Class::Func, workerThread);     // Not OK. Should fail compile.
    dConstClass.Bind(&c, &Class::FuncConst, workerThread);  // OK
    auto rConst = dConstClass();

    // Make sure we get a default constructed return value.
    TestReturn::val = 0;
    DelegateMemberAsync<TestReturnClass, TestReturn()> testRet;
    ASSERT_TRUE(TestReturn::val == 0);
    testRet();
    ASSERT_TRUE(TestReturn::val == 1);

    Class c2;
    DelegateMemberAsync<Class, std::unique_ptr<int>(int)> delUnique;
    auto tmp = delUnique(10);
    ASSERT_TRUE(tmp == nullptr);
    delUnique.Bind(&c2, &Class::FuncUnique, workerThread);
    std::unique_ptr<int> up = delUnique(12);
    ASSERT_TRUE(up == nullptr);  // Async delegate has default return value 

    auto delS1 = MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1, workerThread);
    auto delS2 = MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1_2, workerThread);
    ASSERT_TRUE(!(delS1 == delS2));

#if 0  // DelegateMemberAsync can't be inserted into ordered container
    std::set<Del> setDel;
    setDel.insert(delS1);
    setDel.insert(delS2);
    ASSERT_TRUE(setDel.size() == 2);
#endif

    const TestClass1 tcConst;
    auto delConstCheck = MakeDelegate(&tcConst, &TestClass1::ConstCheck, workerThread);
    auto delConstCheckRetVal = delConstCheck(TEST_INT);
    ASSERT_TRUE(delConstCheckRetVal == 0);

    delS1.Clear();
    ASSERT_TRUE(delS1.Empty());
    std::swap(delS1, delS2);
    ASSERT_TRUE(!delS1.Empty());
    ASSERT_TRUE(delS2.Empty());

    std::function<int(int)> stdFunc = MakeDelegate(&testClass1, &TestClass1::MemberFuncIntWithReturn1, workerThread);
    int stdFuncRetVal = stdFunc(TEST_INT);
    ASSERT_TRUE(stdFuncRetVal == 0);

    SetClassSingleton setClassSingleton;
#if 0
    // ClassSingleton private constructor. Can't use singleton as ref (&),
    // pointer (*), or pointer-to-pointer (**) since async delegate makes 
    // copy of ClassSingleton argument.
    auto& singleton = ClassSingleton::GetInstance();
    auto delRef = MakeDelegate(&setClassSingleton, &SetClassSingleton::Ref, workerThread);
    auto delPtr = MakeDelegate(&setClassSingleton, &SetClassSingleton::Ptr, workerThread);
    auto delPtrPtr = MakeDelegate(&setClassSingleton, &SetClassSingleton::PtrPtr, workerThread);
#endif

    ClassError classError;
    auto noErrorDel = MakeDelegate(&classError, &ClassError::NoError, workerThread);
    auto noErrorDel2 = MakeDelegate(&classError, &ClassError::NoError2, workerThread);
#if 0
    // Causes compiler error. shared_ptr references not allowed; undefined behavior
    // in multi-threaded system.
    auto errorDel = MakeDelegate(&classError, &ClassError::Error, workerThread);
    auto errorDel2 = MakeDelegate(&classError, &ClassError::Error2, workerThread);
    auto errorDel3 = MakeDelegate(&classError, &ClassError::Error3, workerThread);
    auto errorDel4 = MakeDelegate(&classError, &ClassError::Error4, workerThread);
#endif

    // Shared pointer does not copy singleton object; no copy of shared_ptr arg.
    auto singletonSp = ClassSingleton::GetInstanceSp();
    auto delShared = MakeDelegate(&setClassSingleton, &SetClassSingleton::Shared, workerThread);
    delShared(singletonSp);

    // Invoke the type-safe delegates to verify dispatch doesn't crash at runtime.
    auto structParamSp2 = std::make_shared<const StructParam>();
    noErrorDel(structParamSp2);
    noErrorDel2(std::string("hello world"), structParamSp2);

    Class voidTest;
    // Compile error. Invalid to pass void* argument to async target function
#if 0
    // Test void* args
    const char* str = "Hello World!";
    void* voidPtr = (void*)str;
    auto voidPtrNotNullDel = MakeDelegate(&voidTest, &Class::VoidPtrArgNotNull, workerThread);
    voidPtrNotNullDel(voidPtr);
    auto voidPtrNullDel = MakeDelegate(&voidTest, &Class::VoidPtrArgNull, workerThread);
    voidPtrNullDel(nullptr);
#endif

    // Test void* return. Return value is nullptr because return 
    // value is invalid on a non-blocking async delegate
    auto retVoidPtrDel = MakeDelegate(&voidTest, &Class::RetVoidPtr, workerThread);
    auto retVoidPtr = retVoidPtrDel();
    ASSERT_TRUE(retVoidPtr == nullptr);
    const char* retStr = (const char*)retVoidPtr;
    ASSERT_TRUE(retStr == nullptr);

    // Array of delegates
    Del* arr = new Del[2];
    arr[0].Bind(&testClass1, &TestClass1::MemberFuncInt1, workerThread);
    arr[1].Bind(&testClass1, &TestClass1::MemberFuncInt1, workerThread);
    for (int i = 0; i < 2; i++)
        arr[i](TEST_INT);
    delete[] arr;
}

static void DelegateMemberSpAsyncTests()
{
    using Del = DelegateMemberAsync<TestClass1, void(int)>;

    auto testClass1 = std::make_shared<TestClass1>();

    Del delegate1(testClass1, &TestClass1::MemberFuncInt1, workerThread);
    delegate1(TEST_INT);
    std::invoke(delegate1, TEST_INT);

    delegate1.SetPriority(Priority::HIGH);
    auto delegate2 = delegate1;
    ASSERT_TRUE(delegate1 == delegate2);
    ASSERT_TRUE(delegate2.GetPriority() == Priority::HIGH);
    ASSERT_TRUE(!delegate1.Empty());
    ASSERT_TRUE(!delegate2.Empty());

    Del delegate3(testClass1, &TestClass1::MemberFuncInt1, workerThread);
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
    ASSERT_TRUE(delegate5.GetPriority() == Priority::HIGH);
    ASSERT_TRUE(!delegate5.Empty());
    ASSERT_TRUE(delegate1.Empty());

    Del delegate6(testClass1, &TestClass1::MemberFuncInt1, workerThread);
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
    DelegateMemberAsync<const Class, std::uint16_t(void)> dConstClass;
    //dConstClass.Bind(c, &Class::Func, workerThread);     // Not OK. Should fail compile.
    dConstClass.Bind(c, &Class::FuncConst, workerThread);  // OK
    auto rConst = dConstClass();

    // Make sure we get a default constructed return value.
    TestReturn::val = 0;
    DelegateMemberAsync<TestReturnClass, TestReturn()> testRet;
    ASSERT_TRUE(TestReturn::val == 0);
    testRet();
    ASSERT_TRUE(TestReturn::val == 1);

    auto c2 = std::make_shared<Class>();
    DelegateMemberAsync<Class, std::unique_ptr<int>(int)> delUnique;
    auto tmp = delUnique(10);
    ASSERT_TRUE(tmp == nullptr);
    delUnique.Bind(c2, &Class::FuncUnique, workerThread);
    std::unique_ptr<int> up = delUnique(12);
    ASSERT_TRUE(up == nullptr);  // Async delegate has default return value 

    auto delS1 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1, workerThread);
    auto delS2 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1_2, workerThread);
    ASSERT_TRUE(!(delS1 == delS2));

#if 0  // DelegateMemberAsync can't be inserted into ordered container
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

    std::function<int(int)> stdFunc = MakeDelegate(testClass1, &TestClass1::MemberFuncIntWithReturn1, workerThread);
    int stdFuncRetVal = stdFunc(TEST_INT);
    ASSERT_TRUE(stdFuncRetVal == 0);

    // Array of delegates
    Del* arr = new Del[2];
    arr[0].Bind(testClass1, &TestClass1::MemberFuncInt1, workerThread);
    arr[1].Bind(testClass1, &TestClass1::MemberFuncInt1, workerThread);
    for (int i = 0; i < 2; i++)
        arr[i](TEST_INT);
    delete[] arr;
}

static void DelegateMemberAsyncSpTests()
{
    // Define the delegate type explicitly
    using Del = DelegateMemberAsyncSp<TestClass1, void(int)>;

    // 1. Basic Setup
    auto testClass1 = std::make_shared<TestClass1>();

    Del delegate1(testClass1, &TestClass1::MemberFuncInt1, workerThread);

    // Fire and forget (Async)
    delegate1(TEST_INT);
    std::invoke(delegate1, TEST_INT);

    // Allow worker thread time to process (for verification purposes only)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 2. Priority and Copy
    delegate1.SetPriority(Priority::HIGH);
    auto delegate2 = delegate1;
    ASSERT_TRUE(delegate1 == delegate2);
    ASSERT_TRUE(delegate2.GetPriority() == Priority::HIGH);
    ASSERT_TRUE(!delegate1.Empty());
    ASSERT_TRUE(!delegate2.Empty());

    // 3. Assignment
    Del delegate3(testClass1, &TestClass1::MemberFuncInt1, workerThread);
    delegate3 = delegate1;
    ASSERT_TRUE(delegate3 == delegate1);
    ASSERT_TRUE(delegate3);

    delegate3.Clear();
    ASSERT_TRUE(delegate3.Empty());
    ASSERT_TRUE(!delegate3);

    // 4. Cloning
    auto* delegate4 = delegate1.Clone();
    ASSERT_TRUE(*delegate4 == delegate1);
    delete delegate4;

    // 5. Move Semantics
    auto delegate5 = std::move(delegate1);
    ASSERT_TRUE(delegate5.GetPriority() == Priority::HIGH);
    ASSERT_TRUE(!delegate5.Empty());
    ASSERT_TRUE(delegate1.Empty());

    // ==========================================================
    // 6. ASYNC RETURN VALUE TEST
    // ==========================================================
    // Unlike Sync or AsyncWait, standard Async returns the default value immediately.
    // It does NOT wait for the function to execute.
    {
        using DelRet = DelegateMemberAsyncSp<TestClass1, int(int)>;
        DelRet delRet(testClass1, &TestClass1::MemberFuncIntWithReturn1, workerThread);

        // Even though the function returns TEST_INT (e.g. 12345), 
        // the async wrapper should return int() which is 0.
        int retVal = delRet(TEST_INT);
        ASSERT_TRUE(retVal == 0);
    }

    // ==========================================================
        // 7. THE CRITICAL TEST: Object Expiration (Zombie Object)
        // ==========================================================
    {
        using DelZombie = DelegateMemberAsyncSp<TestClass1, void(int)>;
        DelZombie zombieDel;

        {
            // Create a temporary object
            auto tempObj = std::make_shared<TestClass1>();

            // Bind delegate to temporary object
            zombieDel.Bind(tempObj, &TestClass1::MemberFuncInt1, workerThread);

            // Invoke while alive - MUST PASS TEST_INT
            zombieDel(TEST_INT);

            // Give the worker thread a tiny moment to process "zombieDel(TEST_INT)" 
            // while 'tempObj' is definitely still alive.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // 'tempObj' is destroyed here. Ref count drops to 0.
        // 'zombieDel' now holds a weak_ptr to a dead object.

        // Invoke after death:
        // 1. The delegate queues the message.
        // 2. The worker thread tries to lock weak_ptr, fails (nullptr), and ignores it.
        // RESULT: NO CRASH.
        // (The value here doesn't matter because it won't execute, but let's be consistent)
        zombieDel(TEST_INT);

        // Sleep to ensure worker thread processed the "dead" call without crashing
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 8. Const Correctness
    auto c = std::make_shared<const Class>();
    DelegateMemberAsyncSp<const Class, std::uint16_t(void)> dConstClass;
    // dConstClass.Bind(c, &Class::Func, workerThread);      // Not OK. Compile Error.
    dConstClass.Bind(c, &Class::FuncConst, workerThread);    // OK
    auto rConst = dConstClass(); // Returns 0 (default), doesn't wait
    ASSERT_TRUE(rConst == 0);

    // 9. Unique Ptr Return type
    // Async delegates return default constructed values. 
    // std::unique_ptr default constructor is nullptr.
    auto c2 = std::make_shared<Class>();
    DelegateMemberAsyncSp<Class, std::unique_ptr<int>(int)> delUnique;
    delUnique.Bind(c2, &Class::FuncUnique, workerThread);
    std::unique_ptr<int> up = delUnique(12);
    ASSERT_TRUE(up == nullptr);

    // 10. Equality Checks
    // Note: Use MakeDelegate if overloads are available, otherwise direct construction
    // Assuming MakeDelegate overloads for AsyncShared were added:
    // auto delS1 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1, workerThread);
    // auto delS2 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1_2, workerThread);

    // Using direct construction for safety in this test snippet
    Del delS1(testClass1, &TestClass1::MemberFuncInt1, workerThread);
    Del delS2(testClass1, &TestClass1::MemberFuncInt1_2, workerThread);

    ASSERT_TRUE(!(delS1 == delS2));

    delS1.Clear();
    ASSERT_TRUE(delS1.Empty());
    std::swap(delS1, delS2);
    ASSERT_TRUE(!delS1.Empty());
    ASSERT_TRUE(delS2.Empty());
}

static void DelegateFunctionAsyncTests()
{
    using Del = DelegateFunctionAsync<void(int)>;

    Del delegate1(LambdaNoCapture, workerThread);
    delegate1(TEST_INT);
    std::invoke(delegate1, TEST_INT);

    delegate1.SetPriority(Priority::HIGH);
    auto delegate2 = delegate1;
    ASSERT_TRUE(delegate1 == delegate2);
    ASSERT_TRUE(delegate2.GetPriority() == Priority::HIGH);
    ASSERT_TRUE(!delegate1.Empty());
    ASSERT_TRUE(!delegate2.Empty());

    Del delegate3(LambdaNoCapture, workerThread);
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
    ASSERT_TRUE(delegate5.GetPriority() == Priority::HIGH);
    ASSERT_TRUE(!delegate5.Empty());
    ASSERT_TRUE(delegate1.Empty());

    Del delegate6(LambdaNoCapture, workerThread);
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
    DelegateFunctionAsync<std::unique_ptr<int>(int)> delUnique;
    auto tmp = delUnique(10);
    ASSERT_TRUE(tmp == nullptr);
    delUnique.Bind(LambdaUnqiue, workerThread);
    std::unique_ptr<int> up = delUnique(12);
    ASSERT_TRUE(up == nullptr);  // Async delegate has default return value 

    auto delS1 = MakeDelegate(LambdaNoCapture, workerThread);
    auto delS2 = MakeDelegate(LambdaNoCapture2, workerThread);
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
    arr[0].Bind(LambdaNoCapture, workerThread);
    arr[1].Bind(LambdaNoCapture, workerThread);
    for (int i = 0; i < 2; i++)
        arr[i](TEST_INT);
    delete[] arr;

    std::function NoError = [](std::shared_ptr<const StructParam> c) {};
    std::function NoError2 = [](const std::string& s, std::shared_ptr<const StructParam> c) {};
    std::function Error = [](std::shared_ptr<const StructParam>& c) {};
    std::function Error2 = [](const std::shared_ptr<const StructParam>& c) {};
    std::function Error3 = [](std::shared_ptr<const StructParam>* c) {};
    std::function Error4 = [](const std::shared_ptr<const StructParam>* c) {};

    auto noErrorDel = MakeDelegate(NoError, workerThread);
    auto noErrorDel2 = MakeDelegate(NoError2, workerThread);
#if 0
    // Causes compiler error. shared_ptr references not allowed; undefined behavior
    // in multi-threaded system.
    auto errorDel = MakeDelegate(Error, workerThread);
    auto errorDel2 = MakeDelegate(Error2, workerThread);
    auto errorDel3 = MakeDelegate(Error3, workerThread);
    auto errorDel4 = MakeDelegate(Error4, workerThread);
#endif

    // MakeDelegate with raw lambda — no std::function wrapper required
    {
        // Inline raw lambda
        auto d1 = MakeDelegate([](int i) {}, workerThread);
        ASSERT_TRUE(!d1.Empty());
        d1(TEST_INT);

        // Named raw lambda (auto, not std::function)
        auto rawLam = [](int i) {};
        auto d2 = MakeDelegate(rawLam, workerThread);
        ASSERT_TRUE(!d2.Empty());
        d2(TEST_INT);

        // Capturing lambda
        int captured = TEST_INT;
        auto d3 = MakeDelegate([captured](int i) {}, workerThread);
        ASSERT_TRUE(!d3.Empty());
        d3(TEST_INT);
    }
}

void DelegateAsyncTests()
{
    workerThread.CreateThread();

    DelegateFreeAsyncTests();
    DelegateMemberAsyncTests();
    DelegateMemberSpAsyncTests();
    DelegateMemberAsyncSpTests();
    DelegateFunctionAsyncTests();

    workerThread.ExitThread();
}
