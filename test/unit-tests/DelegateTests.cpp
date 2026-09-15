#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include <iostream>
#include <set>
#include <cstring>

using namespace dmq;
using namespace dmq::os;
using namespace std;
using namespace UnitTestData;

namespace Sync
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
        void Func(TestReturn& tr) {}
    };
}
using namespace Sync;

static void DelegateFreeTests()
{
    using Del = DelegateFree<void(int)>;

    Del delegate1(FreeFuncInt1);
    delegate1(TEST_INT);
    std::invoke(delegate1, TEST_INT);

    Delegate<void(int)>* baseDel = new Del(FreeFuncInt1);
    (*baseDel)(TEST_INT);
    delete baseDel;

    auto delegate2 = delegate1;
    DMQ_ASSERT_TRUE(delegate1 == delegate2);
    DMQ_ASSERT_TRUE(!delegate1.Empty());
    DMQ_ASSERT_TRUE(!delegate2.Empty());

    Del delegate3;
    delegate3 = delegate1;
    DMQ_ASSERT_TRUE(delegate3 == delegate1);
    DMQ_ASSERT_TRUE(delegate3);

    delegate3.Clear();
    DMQ_ASSERT_TRUE(delegate3.Empty());
    DMQ_ASSERT_TRUE(!delegate3);

    auto* delegate4 = delegate1.Clone();
    DMQ_ASSERT_TRUE(*delegate4 == delegate1);
    delete delegate4;

    auto delegate5 = std::move(delegate1);
    DMQ_ASSERT_TRUE(!delegate5.Empty());
    DMQ_ASSERT_TRUE(delegate1.Empty());

    Del delegate6;
    delegate6 = std::move(delegate2);
    DMQ_ASSERT_TRUE(!delegate6.Empty());
    DMQ_ASSERT_TRUE(delegate2.Empty());
    DMQ_ASSERT_TRUE(delegate6 != nullptr);
    DMQ_ASSERT_TRUE(nullptr != delegate6);
    DMQ_ASSERT_TRUE(delegate2 == nullptr);
    DMQ_ASSERT_TRUE(nullptr == delegate2);

    // Compare disparate delegate types
    DelegateFunction<void(int)> other;
    DMQ_ASSERT_TRUE(!(delegate6.Equal(other)));

    delegate6 = nullptr;
    DMQ_ASSERT_TRUE(delegate6.Empty());
    DMQ_ASSERT_TRUE(delegate6 == nullptr);

    Del del1{ &FreeFuncInt1 };
    Del del2 = del1;
    Del del3;
    del3 = &FreeFuncInt1;
    DMQ_ASSERT_TRUE(!del1.Empty());
    DMQ_ASSERT_TRUE(!del2.Empty());
    DMQ_ASSERT_TRUE(!del3.Empty());
    DMQ_ASSERT_TRUE(del1 == del2);
    DMQ_ASSERT_TRUE(del2 == del3);

    DelegateFree<std::uint16_t(void)> d;
    DMQ_ASSERT_TRUE(!d);
    auto r = d();
    using ArgT = decltype(r);
#ifndef USE_ALLOCATOR
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::min)() == 0);
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::min)() == 0);
#endif
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::max)() == 0xffff);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_signed == false);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_exact == true);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_integer == true);
    DMQ_ASSERT_TRUE(r == 0);

    // Make sure we get a default constructed return value.
    TestReturn::val = 0;
    DelegateFree<TestReturn()> testRet;
    DMQ_ASSERT_TRUE(TestReturn::val == 0);
    testRet();
    DMQ_ASSERT_TRUE(TestReturn::val == 1);

    DelegateFree<std::unique_ptr<int>(int)> delUnique;
    auto tmp = delUnique(10);
    DMQ_ASSERT_TRUE(tmp == nullptr);
    delUnique.Bind(&FuncUnique);
    std::unique_ptr<int> up = delUnique(12);
    DMQ_ASSERT_TRUE(*up == 12);

    auto delS1 = MakeDelegate(FreeFuncInt1);
    auto delS2 = MakeDelegate(FreeFuncInt1_2);
    DMQ_ASSERT_TRUE(!(delS1 == delS2));

    std::set<Del> setDel;
    setDel.insert(delS1);
    setDel.insert(delS2);
    DMQ_ASSERT_TRUE(setDel.size() == 2);

    delS1.Clear();
    DMQ_ASSERT_TRUE(delS1.Empty());
    std::swap(delS1, delS2);
    DMQ_ASSERT_TRUE(!delS1.Empty());
    DMQ_ASSERT_TRUE(delS2.Empty());
    delS1.Clear();
    DMQ_ASSERT_TRUE(delS1 == delS2);

    std::function<int(int)> stdFunc = MakeDelegate(&FreeFuncIntWithReturn1);
    int stdFuncRetVal = stdFunc(TEST_INT);
    DMQ_ASSERT_TRUE(stdFuncRetVal == TEST_INT);

    // ClassSingleton private constructor. Sync delegate does not make
    // copy of ClassSingleton argument; all setter functions ok.
    auto& singleton = ClassSingleton::GetInstance();
    auto delRef = MakeDelegate(&SetClassSingletonRef);
    auto delPtr = MakeDelegate(&SetClassSingletonPtr);
    auto delPtrPtr = MakeDelegate(&SetClassSingletonPtrPtr);

    // Shared pointer does not copy singleton object; no copy of shared_ptr arg.
    auto singletonSp = ClassSingleton::GetInstanceSp();
    auto delShared = MakeDelegate(&SetClassSingletonShared);
    delShared(singletonSp);

    // Test outgoing ptr argument
    StructParam sparam;
    int iparam = 100;
    sparam.val = TEST_INT;
    auto outgoingArg = MakeDelegate(&OutgoingPtrArg);
    outgoingArg(&sparam, &iparam);
    DMQ_ASSERT_TRUE(sparam.val == TEST_INT + 1);
    DMQ_ASSERT_TRUE(iparam == 101);

    // Test outgoing ptr-ptr argument
    StructParam* psparam = nullptr;
    auto outgoingArg2 = MakeDelegate(&OutgoingPtrPtrArg);
    outgoingArg2(&psparam);
    DMQ_ASSERT_TRUE(psparam->val == TEST_INT);
    delete psparam;

    // Test outgoing ref argument
    sparam.val = TEST_INT;
    auto outgoingArg3 = MakeDelegate(&OutgoingRefArg);
    outgoingArg3(sparam);
    DMQ_ASSERT_TRUE(sparam.val == TEST_INT + 1);

    // Sync invoke does not copy Class object when passed to func
    Class classInstance;
    Class::m_construtorCnt = 0;
    auto cntDel = MakeDelegate(&ConstructorCnt);
    cntDel(&classInstance);
    DMQ_ASSERT_TRUE(Class::m_construtorCnt == 0);

    // Test void* args
    const char* str = "Hello World!";
    void* voidPtr = (void*)str;
    auto voidPtrNotNullDel = MakeDelegate(&VoidPtrArgNotNull);
    voidPtrNotNullDel(voidPtr);
    auto voidPtrNullDel = MakeDelegate(&VoidPtrArgNull);
    voidPtrNullDel(nullptr);

    // Test void* return
    auto retVoidPtrDel = MakeDelegate(&RetVoidPtr);
    auto retVoidPtr = retVoidPtrDel();
    DMQ_ASSERT_TRUE(retVoidPtr != nullptr);
    const char* retStr = (const char*)retVoidPtr;
    DMQ_ASSERT_TRUE(strcmp(retStr, "Hello World!") == 0);

    // Test rvalue ref
    auto rvalueRefDel = MakeDelegate(&FuncRvalueRef);
    int rv = TEST_INT;
    rvalueRefDel(std::move(rv));
    rvalueRefDel(12345678);

    // Array of delegates
    Del* arr = new Del[2];
    arr[0].Bind(FreeFuncInt1);
    arr[1].Bind(FreeFuncInt1);
    for (int i = 0; i < 2; i++)
        arr[i](TEST_INT);
    delete[] arr;
}

static void DelegateMemberTests()
{
    using Del = DelegateMember<TestClass1, void(int)>;

    TestClass1 testClass1;

    Del delegate1(&testClass1, &TestClass1::MemberFuncInt1);
    delegate1(TEST_INT);
    std::invoke(delegate1, TEST_INT);

    auto delegate2 = delegate1;
    DMQ_ASSERT_TRUE(delegate1 == delegate2);
    DMQ_ASSERT_TRUE(!delegate1.Empty());
    DMQ_ASSERT_TRUE(!delegate2.Empty());

    Del delegate3;
    delegate3 = delegate1;
    DMQ_ASSERT_TRUE(delegate3 == delegate1);
    DMQ_ASSERT_TRUE(delegate3);

    delegate3.Clear();
    DMQ_ASSERT_TRUE(delegate3.Empty());
    DMQ_ASSERT_TRUE(!delegate3);

    auto* delegate4 = delegate1.Clone();
    DMQ_ASSERT_TRUE(*delegate4 == delegate1);
    delete delegate4;

    auto delegate5 = std::move(delegate1);
    DMQ_ASSERT_TRUE(!delegate5.Empty());
    DMQ_ASSERT_TRUE(delegate1.Empty());

    Del delegate6;
    delegate6 = std::move(delegate2);
    DMQ_ASSERT_TRUE(!delegate6.Empty());
    DMQ_ASSERT_TRUE(delegate2.Empty());
    DMQ_ASSERT_TRUE(delegate6 != nullptr);
    DMQ_ASSERT_TRUE(nullptr != delegate6);
    DMQ_ASSERT_TRUE(delegate2 == nullptr);
    DMQ_ASSERT_TRUE(nullptr == delegate2);

    // Compare disparate delegate types
    DelegateFunction<void(int)> other;
    DMQ_ASSERT_TRUE(!(delegate6.Equal(other)));

    delegate6 = nullptr;
    DMQ_ASSERT_TRUE(delegate6.Empty());
    DMQ_ASSERT_TRUE(delegate6 == nullptr);

    Base* base = new Derive;
    DelegateMember<Base, int(void)> delegate7;
    delegate7 = MakeDelegate(base, &Base::Func);
    DMQ_ASSERT_TRUE(delegate7() == TEST_INT);
    delete base;

    DelegateMember<Class, std::uint16_t(void)> d;
    DMQ_ASSERT_TRUE(!d);
    auto r = d();
    using ArgT = decltype(r);
#ifndef USE_ALLOCATOR
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::min)() == 0);
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::min)() == 0);
#endif
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::max)() == 0xffff);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_signed == false);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_exact == true);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_integer == true);
    DMQ_ASSERT_TRUE(r == 0);

    // Check for const correctness
    const Class c;
    DelegateMember<const Class, std::uint16_t(void)> dConstClass;
    //dConstClass.Bind(&c, &Class::Func);     // Not OK. Should fail compile.
    dConstClass.Bind(&c, &Class::FuncConst);  // OK
    auto rConst = dConstClass();

    // Make sure we get a default constructed return value.
    TestReturn::val = 0;
    DelegateMember<TestReturnClass, TestReturn()> testRet;
    DMQ_ASSERT_TRUE(TestReturn::val == 0);
    testRet();
    DMQ_ASSERT_TRUE(TestReturn::val == 1);

    // Temporary object function argument not allowed
    //DelegateMember<TestReturnClass, void(TestReturn&)> testArg;
    //testArg(TestReturn());

    Class c2;
    DelegateMember<Class, std::unique_ptr<int>(int)> delUnique;
    auto tmp = delUnique(10);
    DMQ_ASSERT_TRUE(tmp == nullptr);
    delUnique.Bind(&c2, &Class::FuncUnique);
    std::unique_ptr<int> up = delUnique(12);
    DMQ_ASSERT_TRUE(*up == 12);

    auto delS1 = MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1);
    auto delS2 = MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1_2);
    DMQ_ASSERT_TRUE(!(delS1 == delS2));

#if 0  // DelegateMember can't be inserted into ordered container
    std::set<Del> setDel;
    setDel.insert(delS1);
    setDel.insert(delS2);
    DMQ_ASSERT_TRUE(setDel.size() == 2);
#endif

    delS1.Clear();
    DMQ_ASSERT_TRUE(delS1.Empty());
    std::swap(delS1, delS2);
    DMQ_ASSERT_TRUE(!delS1.Empty());
    DMQ_ASSERT_TRUE(delS2.Empty());
    delS1.Clear();
    DMQ_ASSERT_TRUE(delS1 == delS2);

    const TestClass1 tcConst;
    auto delConstCheck = MakeDelegate(&tcConst, &TestClass1::ConstCheck);
    auto delConstCheckRetVal = delConstCheck(TEST_INT);
    DMQ_ASSERT_TRUE(delConstCheckRetVal == TEST_INT);

    std::function<int(int)> stdFunc = MakeDelegate(&testClass1, &TestClass1::MemberFuncIntWithReturn1);
    int stdFuncRetVal = stdFunc(TEST_INT);
    DMQ_ASSERT_TRUE(stdFuncRetVal == TEST_INT);

    SetClassSingleton setClassSingleton;
    // ClassSingleton private constructor. Sync delegate does not make
    // copy of ClassSingleton argument; all setter functions ok.
    auto& singleton = ClassSingleton::GetInstance();
    auto delRef = MakeDelegate(&setClassSingleton, &SetClassSingleton::Ref);
    auto delPtr = MakeDelegate(&setClassSingleton, &SetClassSingleton::Ptr);
    auto delPtrPtr = MakeDelegate(&setClassSingleton, &SetClassSingleton::PtrPtr);

    // Shared pointer does not copy singleton object; no copy of shared_ptr arg.
    auto singletonSp = ClassSingleton::GetInstanceSp();
    auto delShared = MakeDelegate(&setClassSingleton, &SetClassSingleton::Shared);
    delShared(singletonSp);

    // Test void* args
    Class voidTest;
    const char* str = "Hello World!";
    void* voidPtr = (void*)str;
    auto voidPtrNotNullDel = MakeDelegate(&voidTest, &Class::VoidPtrArgNotNull);
    voidPtrNotNullDel(voidPtr);
    auto voidPtrNullDel = MakeDelegate(&voidTest, &Class::VoidPtrArgNull);
    voidPtrNullDel(nullptr);

    // Test void* return
    auto retVoidPtrDel = MakeDelegate(&voidTest, &Class::RetVoidPtr);
    auto retVoidPtr = retVoidPtrDel();
    DMQ_ASSERT_TRUE(retVoidPtr != nullptr);
    const char* retStr = (const char*)retVoidPtr;
    DMQ_ASSERT_TRUE(strcmp(retStr, "Hello World!") == 0);

    // Test rvalue ref
    auto rvalueRefDel = MakeDelegate(&testClass1, &TestClass1::FuncRvalueRef);
    int rv = TEST_INT;
    rvalueRefDel(std::move(rv));
    rvalueRefDel(12345678);

    // Array of delegates
    Del* arr = new Del[2];
    arr[0].Bind(&testClass1, &TestClass1::MemberFuncInt1);
    arr[1].Bind(&testClass1, &TestClass1::MemberFuncInt1);
    for (int i = 0; i < 2; i++)
        arr[i](TEST_INT);
    delete[] arr;
}

static void DelegateMemberSharedTests()
{
    using Del = DelegateMember<TestClass1, void(int)>;

    auto testClass1 = std::make_shared<TestClass1>();

    Del delegate1(testClass1, &TestClass1::MemberFuncInt1);
    delegate1(TEST_INT);
    std::invoke(delegate1, TEST_INT);

    auto delegate2 = delegate1;
    DMQ_ASSERT_TRUE(delegate1 == delegate2);
    DMQ_ASSERT_TRUE(!delegate1.Empty());
    DMQ_ASSERT_TRUE(!delegate2.Empty());

    Del delegate3;
    delegate3 = delegate1;
    DMQ_ASSERT_TRUE(delegate3 == delegate1);
    DMQ_ASSERT_TRUE(delegate3);

    delegate3.Clear();
    DMQ_ASSERT_TRUE(delegate3.Empty());
    DMQ_ASSERT_TRUE(!delegate3);

    auto* delegate4 = delegate1.Clone();
    DMQ_ASSERT_TRUE(*delegate4 == delegate1);
    delete delegate4;

    auto delegate5 = std::move(delegate1);
    DMQ_ASSERT_TRUE(!delegate5.Empty());
    DMQ_ASSERT_TRUE(delegate1.Empty());

    Del delegate6;
    delegate6 = std::move(delegate2);
    DMQ_ASSERT_TRUE(!delegate6.Empty());
    DMQ_ASSERT_TRUE(delegate2.Empty());
    DMQ_ASSERT_TRUE(delegate6 != nullptr);
    DMQ_ASSERT_TRUE(nullptr != delegate6);
    DMQ_ASSERT_TRUE(delegate2 == nullptr);
    DMQ_ASSERT_TRUE(nullptr == delegate2);

    // Compare disparate delegate types
    DelegateFunction<void(int)> other;
    DMQ_ASSERT_TRUE(!(delegate6.Equal(other)));

    delegate6 = nullptr;
    DMQ_ASSERT_TRUE(delegate6.Empty());
    DMQ_ASSERT_TRUE(delegate6 == nullptr);

    std::shared_ptr<Base> base = std::make_shared<Derive>();
    DelegateMemberSp<Base, int(void)> delegate7;
    delegate7 = MakeDelegate(base, &Base::Func);
    DMQ_ASSERT_TRUE(delegate7() == TEST_INT);

    DelegateMember<Class, std::uint16_t(void)> d;
    DMQ_ASSERT_TRUE(!d);
    auto r = d();
    using ArgT = decltype(r);
#ifndef USE_ALLOCATOR
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::min)() == 0);
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::min)() == 0);
#endif
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::max)() == 0xffff);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_signed == false);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_exact == true);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_integer == true);
    DMQ_ASSERT_TRUE(r == 0);

    // Check for const correctness
    auto c = std::make_shared<const Class>();
    DelegateMember<const Class, std::uint16_t(void)> dConstClass;
    //dConstClass.Bind(c, &Class::Func);     // Not OK. Should fail compile.
    dConstClass.Bind(c, &Class::FuncConst);  // OK
    auto rConst = dConstClass();

    // Make sure we get a default constructed return value.
    TestReturn::val = 0;
    DelegateMember<TestReturnClass, TestReturn()> testRet;
    DMQ_ASSERT_TRUE(TestReturn::val == 0);
    testRet();
    DMQ_ASSERT_TRUE(TestReturn::val == 1);

    auto c2 = std::make_shared<Class>();
    DelegateMember<Class, std::unique_ptr<int>(int)> delUnique;
    auto tmp = delUnique(10);
    DMQ_ASSERT_TRUE(tmp == nullptr);
    delUnique.Bind(c2, &Class::FuncUnique);
    std::unique_ptr<int> up = delUnique(12);
    DMQ_ASSERT_TRUE(*up == 12);

    auto delS1 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1);
    auto delS2 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1_2);
    DMQ_ASSERT_TRUE(!(delS1 == delS2));

#if 0  // DelegateMember can't be inserted into ordered container
    std::set<Del> setDel;
    setDel.insert(delS1);
    setDel.insert(delS2);
    DMQ_ASSERT_TRUE(setDel.size() == 2);
#endif

    delS1.Clear();
    DMQ_ASSERT_TRUE(delS1.Empty());
    std::swap(delS1, delS2);
    DMQ_ASSERT_TRUE(!delS1.Empty());
    DMQ_ASSERT_TRUE(delS2.Empty());
    delS1.Clear();
    DMQ_ASSERT_TRUE(delS1 == delS2);

    std::function<int(int)> stdFunc = MakeDelegate(testClass1, &TestClass1::MemberFuncIntWithReturn1);
    int stdFuncRetVal = stdFunc(TEST_INT);
    DMQ_ASSERT_TRUE(stdFuncRetVal == TEST_INT);

    // Array of delegates
    Del* arr = new Del[2];
    arr[0].Bind(testClass1, &TestClass1::MemberFuncInt1);
    arr[1].Bind(testClass1, &TestClass1::MemberFuncInt1);
    for (int i = 0; i < 2; i++)
        arr[i](TEST_INT);
    delete[] arr;
}

static void DelegateMemberSpTests()
{
    // 1. Basic Setup using DelegateMemberSp
    using Del = DelegateMemberSp<TestClass1, void(int)>;

    // Create a shared pointer
    auto testClass1 = std::make_shared<TestClass1>();

    // Constructor binding
    Del delegate1(testClass1, &TestClass1::MemberFuncInt1);
    delegate1(TEST_INT);
    std::invoke(delegate1, TEST_INT);

    // 2. Copy and Assignment
    auto delegate2 = delegate1;
    DMQ_ASSERT_TRUE(delegate1 == delegate2);
    DMQ_ASSERT_TRUE(!delegate1.Empty());
    DMQ_ASSERT_TRUE(!delegate2.Empty());

    Del delegate3;
    delegate3 = delegate1;
    DMQ_ASSERT_TRUE(delegate3 == delegate1);
    DMQ_ASSERT_TRUE(delegate3);

    delegate3.Clear();
    DMQ_ASSERT_TRUE(delegate3.Empty());
    DMQ_ASSERT_TRUE(!delegate3);

    // 3. Cloning
    auto* delegate4 = delegate1.Clone();
    DMQ_ASSERT_TRUE(*delegate4 == delegate1);
    delete delegate4;

    // 4. Move Semantics
    auto delegate5 = std::move(delegate1);
    DMQ_ASSERT_TRUE(!delegate5.Empty());
    DMQ_ASSERT_TRUE(delegate1.Empty());

    // 5. MakeDelegate Helper
    auto delS1 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1);
    auto delS2 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1_2);
    DMQ_ASSERT_TRUE(!(delS1 == delS2));

    // 6. Const Correctness
    auto c = std::make_shared<const Class>();
    DelegateMemberSp<const Class, std::uint16_t(void)> dConstClass;
    // dConstClass.Bind(c, &Class::Func);      // Not OK. Should fail compile (const object, non-const func).
    dConstClass.Bind(c, &Class::FuncConst);    // OK
    auto rConst = dConstClass();
    DMQ_ASSERT_TRUE(rConst == 0); // Assuming FuncConst returns 0

    // ==========================================================
    // 7. THE CRITICAL TEST: Object Expiration (Weak Pointer Check)
    // ==========================================================
    {
        // Define a delegate that returns an int
        DelegateMemberSp<TestClass1, int(int)> d_expired;

        {
            // Create a temporary object
            auto tempObj = std::make_shared<TestClass1>();

            // Bind delegate to temporary object
            d_expired = MakeDelegate(tempObj, &TestClass1::MemberFuncIntWithReturn1);

            // Invoke while alive: Should return TEST_INT
            int retAlive = d_expired(TEST_INT);
            DMQ_ASSERT_TRUE(retAlive == TEST_INT);

            // Prove it is valid
            DMQ_ASSERT_TRUE(!d_expired.Empty());
        }
        // 'tempObj' is now destroyed. 'd_expired' holds a weak_ptr to a dead object.

        // Invoke after death: 
        // 1. Should NOT CRASH.
        // 2. Should return default constructed value (0 for int).
        int retDead = d_expired(TEST_INT);
        DMQ_ASSERT_TRUE(retDead == 0);
    }

    // 8. Test void return on expired object
    {
        DelegateMemberSp<TestClass1, void(int)> d_void_expired;
        {
            auto tempObj = std::make_shared<TestClass1>();
            d_void_expired = MakeDelegate(tempObj, &TestClass1::MemberFuncInt1);
            d_void_expired(TEST_INT); // Works
        }
        // Object dead. Call should be safe no-op.
        d_void_expired(TEST_INT);
    }

    // 9. Inheritance
    std::shared_ptr<Base> base = std::make_shared<Derive>();
    DelegateMemberSp<Base, int(void)> delegate7;
    delegate7 = MakeDelegate(base, &Base::Func);
    DMQ_ASSERT_TRUE(delegate7() == TEST_INT);

    // 10. Unique Ptr Return type
    // Note: DelegateMemberSp usually requires default constructible return types
    // because it must return *something* if the object is dead. 
    // std::unique_ptr is default constructible (nullptr), so this works.
    auto c2 = std::make_shared<Class>();
    DelegateMemberSp<Class, std::unique_ptr<int>(int)> delUnique;
    delUnique.Bind(c2, &Class::FuncUnique);
    std::unique_ptr<int> up = delUnique(12);
    DMQ_ASSERT_TRUE(up != nullptr);
    DMQ_ASSERT_TRUE(*up == 12);

    // Test unique ptr on dead object
    c2.reset(); // Kill object
    std::unique_ptr<int> upDead = delUnique(12);
    DMQ_ASSERT_TRUE(upDead == nullptr); // Should return default std::unique_ptr (nullptr)
}

static void DelegateFunctionTests()
{
    using Del = DelegateFunction<void(int)>;

    Del delegate1(LambdaNoCapture);
    delegate1(TEST_INT);
    std::invoke(delegate1, TEST_INT);

    auto delegate2 = delegate1;
    DMQ_ASSERT_TRUE(delegate1 == delegate2);
    DMQ_ASSERT_TRUE(!delegate1.Empty());
    DMQ_ASSERT_TRUE(!delegate2.Empty());

    Del delegate3;
    delegate3 = delegate1;
    DMQ_ASSERT_TRUE(delegate3 == delegate1);
    DMQ_ASSERT_TRUE(delegate3);

    delegate3.Clear();
    DMQ_ASSERT_TRUE(delegate3.Empty());
    DMQ_ASSERT_TRUE(!delegate3);

    auto* delegate4 = delegate1.Clone();
    DMQ_ASSERT_TRUE(*delegate4 == delegate1);
    delete delegate4;

    auto delegate5 = std::move(delegate1);
    DMQ_ASSERT_TRUE(!delegate5.Empty());
    DMQ_ASSERT_TRUE(delegate1.Empty());

    Del delegate6;
    delegate6 = std::move(delegate2);
    DMQ_ASSERT_TRUE(!delegate6.Empty());
    DMQ_ASSERT_TRUE(delegate2.Empty());
    DMQ_ASSERT_TRUE(delegate6 != nullptr);
    DMQ_ASSERT_TRUE(nullptr != delegate6);
    DMQ_ASSERT_TRUE(delegate2 == nullptr);
    DMQ_ASSERT_TRUE(nullptr == delegate2);

    // Compare disparate delegate types
    DelegateFree<void(int)> other;
    DMQ_ASSERT_TRUE(!(delegate6.Equal(other)));

    delegate6 = nullptr;
    DMQ_ASSERT_TRUE(delegate6.Empty());
    DMQ_ASSERT_TRUE(delegate6 == nullptr);
    DMQ_ASSERT_TRUE(nullptr == delegate6);
    DMQ_ASSERT_TRUE(!(delegate6 != nullptr));

    Del del1{ LambdaNoCapture };
    Del del2 = del1;
    Del del3;
    del3 = LambdaNoCapture;
    DMQ_ASSERT_TRUE(!del1.Empty());
    DMQ_ASSERT_TRUE(!del2.Empty());
    DMQ_ASSERT_TRUE(!del3.Empty());
    DMQ_ASSERT_TRUE(del1 == del2);
    DMQ_ASSERT_TRUE(del2 == del3);

    DelegateFunction<std::uint16_t(void)> d;
    DMQ_ASSERT_TRUE(!d);
    auto r = d();
    using ArgT = decltype(r);
#ifndef USE_ALLOCATOR
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::min)() == 0);
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::min)() == 0);
#endif
    DMQ_ASSERT_TRUE((std::numeric_limits<ArgT>::max)() == 0xffff);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_signed == false);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_exact == true);
    DMQ_ASSERT_TRUE(std::numeric_limits<ArgT>::is_integer == true);
    DMQ_ASSERT_TRUE(r == 0);

    // Make sure we get a default constructed return value.
    TestReturn::val = 0;
    DelegateFunction<TestReturn()> testRet;
    DMQ_ASSERT_TRUE(TestReturn::val == 0);
    testRet();
    DMQ_ASSERT_TRUE(TestReturn::val == 1);

    auto c2 = std::make_shared<Class>();
    DelegateFunction<std::unique_ptr<int>(int)> delUnique;
    auto tmp = delUnique(10);
    DMQ_ASSERT_TRUE(tmp == nullptr);
    delUnique.Bind(LambdaUnqiue);
    std::unique_ptr<int> up = delUnique(12);
    DMQ_ASSERT_TRUE(*up == 12);

    auto delS1 = MakeDelegate(LambdaNoCapture);
    auto delS2 = MakeDelegate(LambdaNoCapture2);
    //DMQ_ASSERT_TRUE(!(delS1 == delS2));  // std::function can't distriguish difference

    delS1.Clear();
    DMQ_ASSERT_TRUE(delS1.Empty());
    std::swap(delS1, delS2);
    DMQ_ASSERT_TRUE(!delS1.Empty());
    DMQ_ASSERT_TRUE(delS2.Empty());
    delS1.Clear();
    DMQ_ASSERT_TRUE(delS1 == delS2);

    std::set<Del> setDel;
    setDel.insert(delS1);
    setDel.insert(delS2);
    DMQ_ASSERT_TRUE(setDel.size() == 1);

    // Array of delegates
    Del* arr = new Del[2];
    arr[0].Bind(LambdaNoCapture);
    arr[1].Bind(LambdaNoCapture);
    for (int i = 0; i < 2; i++)
        arr[i](TEST_INT);
    delete[] arr;

    {
        using Del = DelegateFunction<int(int)>;

        Del del4([](int x) -> int { return x + 7; });
        DMQ_ASSERT_TRUE(del4(1) == 8);

        Del del5 = Del{ [](int x) -> int { return x + 9; } };
        DMQ_ASSERT_TRUE(del5(1) == 10);

        int t = 5;
        Del del6 = Del{ [t](int x) -> int { return x + 9 + t; } };
        DMQ_ASSERT_TRUE(del6(1) == 15);
    }

    {
        using Del = DelegateFunction<int()>;

        auto lam = []() { return 42; };
        Del del1{ lam };
        Del del2; //= lam;
        del2 = std::function(lam);
        DMQ_ASSERT_TRUE(!del1.Empty());
        DMQ_ASSERT_TRUE(del1() == 42);
        DMQ_ASSERT_TRUE(!del2.Empty());
        DMQ_ASSERT_TRUE(del2() == 42);

#if 0
        Del del3 = lam;
        Del del13 = []() { return 42; };
        DMQ_ASSERT_TRUE(!del3.Empty());
        DMQ_ASSERT_TRUE(del3() == 42);
        DMQ_ASSERT_TRUE(!del13.Empty());
        DMQ_ASSERT_TRUE(del13() == 42);
#endif

        Del del10{ lam };
        Del del11 = { lam };
        Del del12 = { []() { return 42; } };
        DMQ_ASSERT_TRUE(!del10.Empty());
        DMQ_ASSERT_TRUE(del10() == 42);
        DMQ_ASSERT_TRUE(!del11.Empty());
        DMQ_ASSERT_TRUE(del11() == 42);
        DMQ_ASSERT_TRUE(!del12.Empty());
        DMQ_ASSERT_TRUE(del12() == 42);

        auto const lam2 = []() { return 42; };
        Del del4{ lam2 };
        Del del5; // = lam2;
        del5 = std::function(lam2);
        DMQ_ASSERT_TRUE(!del4.Empty());
        DMQ_ASSERT_TRUE(del4() == 42);
        DMQ_ASSERT_TRUE(!del5.Empty());
        DMQ_ASSERT_TRUE(del5() == 42);
    }

    // MakeDelegate with raw lambda — no std::function wrapper required
    {
        // Inline raw lambda
        auto d1 = MakeDelegate([](int i) { DMQ_ASSERT_TRUE(i == TEST_INT); });
        DMQ_ASSERT_TRUE(!d1.Empty());
        d1(TEST_INT);

        // Named raw lambda (auto, not std::function)
        auto rawLam = [](int i) { DMQ_ASSERT_TRUE(i == TEST_INT); };
        auto d2 = MakeDelegate(rawLam);
        DMQ_ASSERT_TRUE(!d2.Empty());
        d2(TEST_INT);

        // Capturing lambda
        int expected = TEST_INT;
        auto d3 = MakeDelegate([expected](int i) { DMQ_ASSERT_TRUE(i == expected); });
        DMQ_ASSERT_TRUE(!d3.Empty());
        d3(TEST_INT);

        // Raw lambda with return value
        auto d4 = MakeDelegate([]() -> int { return TEST_INT; });
        DMQ_ASSERT_TRUE(!d4.Empty());
        DMQ_ASSERT_TRUE(d4() == TEST_INT);
    }

    // Trait checks: is_callable excludes std::function and function pointers
    {
        auto lam = [](int) {};
        static_assert(trait::is_callable<decltype(lam)>::value,
            "raw lambda should satisfy is_callable");
        static_assert(!trait::is_callable<std::function<void(int)>>::value,
            "std::function should not satisfy is_callable");
        static_assert(!trait::is_callable<void(*)(int)>::value,
            "function pointer should not satisfy is_callable");
        static_assert(trait::is_std_function<std::function<void(int)>>::value,
            "should detect std::function specialization");
        static_assert(!trait::is_std_function<decltype(lam)>::value,
            "raw lambda should not match is_std_function");
    }
}

void DelegateTests()
{
    DelegateFreeTests();
    DelegateMemberTests();
    DelegateMemberSharedTests();    // Tests raw DelegateMember with shared_ptr passed in
    DelegateMemberSpTests();        // Tests the actual WeakPtr delegate
    DelegateFunctionTests();
}
