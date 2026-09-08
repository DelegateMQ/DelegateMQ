#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include <iostream>
#ifdef _WIN32
#include <Windows.h>
#endif

using namespace dmq;
using namespace dmq::os;

Thread testThread("DelegateUnitTestsThread");

static const int TEST_INT  = 12345678;

struct StructParam { int val; };
int FreeFuncIntWithReturn0() { return TEST_INT; }

void FreeFunc0() { }

void FreeFuncInt1(int i) { ASSERT_TRUE(i == TEST_INT); }
int FreeFuncIntWithReturn1(int i) { ASSERT_TRUE(i == TEST_INT); return i; }
void FreeFuncPtrPtr1(StructParam** s) { ASSERT_TRUE((*s)->val == TEST_INT); }
void FreeFuncStruct1(StructParam s) { ASSERT_TRUE(s.val == TEST_INT); }
void FreeFuncStructPtr1(StructParam* s) { ASSERT_TRUE(s->val == TEST_INT); }
void FreeFuncStructConstPtr1(const StructParam* s) { ASSERT_TRUE(s->val == TEST_INT); }
void FreeFuncStructRef1(StructParam& s) { ASSERT_TRUE(s.val == TEST_INT); }
void FreeFuncStructConstRef1(const StructParam& s) { ASSERT_TRUE(s.val == TEST_INT); }

void FreeFuncInt2(int i, int i2) { ASSERT_TRUE(i == TEST_INT); ASSERT_TRUE(i2 == TEST_INT); }
int FreeFuncIntWithReturn2(int i, int i2) { ASSERT_TRUE(i == TEST_INT); return i; }
void FreeFuncPtrPtr2(StructParam** s, int i) { ASSERT_TRUE((*s)->val == TEST_INT); }
void FreeFuncStruct2(StructParam s, int i) { ASSERT_TRUE(s.val == TEST_INT); }
void FreeFuncStructPtr2(StructParam* s, int i) { ASSERT_TRUE(s->val == TEST_INT); }
void FreeFuncStructConstPtr2(const StructParam* s, int i) { ASSERT_TRUE(s->val == TEST_INT); }
void FreeFuncStructRef2(StructParam& s, int i) { ASSERT_TRUE(s.val == TEST_INT); }
void FreeFuncStructConstRef2(const StructParam& s, int i) { ASSERT_TRUE(s.val == TEST_INT); }

class TestClass0
{
public:
	void MemberFunc0() { }
	void MemberFunc0Const() const { }
	int MemberFuncWithReturn0() { return TEST_INT; }

	static void StaticFunc0() { }
};

class TestClass1
{
public:
	void MemberFuncInt1(int i) { ASSERT_TRUE(i == TEST_INT); }
	void MemberFuncInt1Const(int i) const { ASSERT_TRUE(i == TEST_INT); }
	int MemberFuncIntWithReturn1(int i) { ASSERT_TRUE(i == TEST_INT); return i; }
	void MemberFuncStruct1(StructParam s) { ASSERT_TRUE(s.val == TEST_INT); }
	void MemberFuncStructPtr1(StructParam* s) { ASSERT_TRUE(s->val == TEST_INT); }
	void MemberFuncStructPtrPtr1(StructParam** s) { ASSERT_TRUE((*s)->val == TEST_INT); }
	void MemberFuncStructConstPtr1(const StructParam* s) { ASSERT_TRUE(s->val == TEST_INT); }
	void MemberFuncStructRef1(StructParam& s) { ASSERT_TRUE(s.val == TEST_INT); }
	void MemberFuncStructConstRef1(const StructParam& s) { ASSERT_TRUE(s.val == TEST_INT); }

	static void StaticFuncInt1(int i) { ASSERT_TRUE(i == TEST_INT); }
	static void StaticFuncStruct1(StructParam s) { ASSERT_TRUE(s.val == TEST_INT); }
	static void StaticFuncStructPtr1(StructParam* s) { ASSERT_TRUE(s->val == TEST_INT); }
	static void StaticFuncStructConstPtr1(const StructParam* s) { ASSERT_TRUE(s->val == TEST_INT); }
	static void StaticFuncStructRef1(StructParam& s) { ASSERT_TRUE(s.val == TEST_INT); }
	static void StaticFuncStructConstRef1(const StructParam& s) { ASSERT_TRUE(s.val == TEST_INT); }
};

class TestClass2
{
public:
	void MemberFuncInt2(int i, int i2) { ASSERT_TRUE(i == TEST_INT); }
	void MemberFuncInt2Const(int i, int i2) const { ASSERT_TRUE(i == TEST_INT); }
	int MemberFuncIntWithReturn2(int i, int i2) { ASSERT_TRUE(i == TEST_INT); return i; }
	void MemberFuncStruct2(StructParam s, int i) { ASSERT_TRUE(s.val == TEST_INT); }
	void MemberFuncStructPtr2(StructParam* s, int i) { ASSERT_TRUE(s->val == TEST_INT); }
	void MemberFuncStructPtrPtr2(StructParam** s, int i) { ASSERT_TRUE((*s)->val == TEST_INT); }
	void MemberFuncStructConstPtr2(const StructParam* s, int i) { ASSERT_TRUE(s->val == TEST_INT); }
	void MemberFuncStructRef2(StructParam& s, int i) { ASSERT_TRUE(s.val == TEST_INT); }
	void MemberFuncStructConstRef2(const StructParam& s, int i) { ASSERT_TRUE(s.val == TEST_INT); }

	static void StaticFuncInt2(int i, int i2) { ASSERT_TRUE(i == TEST_INT); }
	static void StaticFuncStruct2(StructParam s, int i) { ASSERT_TRUE(s.val == TEST_INT); }
	static void StaticFuncStructPtr2(StructParam* s, int i) { ASSERT_TRUE(s->val == TEST_INT); }
	static void StaticFuncStructConstPtr2(const StructParam* s, int i) { ASSERT_TRUE(s->val == TEST_INT); }
	static void StaticFuncStructRef2(StructParam& s, int i) { ASSERT_TRUE(s.val == TEST_INT); }
	static void StaticFuncStructConstRef2(const StructParam& s, int i) { ASSERT_TRUE(s.val == TEST_INT); }
};

class TestClass3
{
public:
	void MemberFuncInt3(int i, int i2, int i3) { ASSERT_TRUE(i == TEST_INT); }
	void MemberFuncInt3Const(int i, int i2, int i3) const { ASSERT_TRUE(i == TEST_INT); }
	int MemberFuncIntWithReturn3(int i, int i2, int i3) { ASSERT_TRUE(i == TEST_INT); return i; }
	void MemberFuncStruct3(StructParam s, int i, int i2) { ASSERT_TRUE(s.val == TEST_INT); }
	void MemberFuncStructPtr3(StructParam* s, int i, int i2) { ASSERT_TRUE(s->val == TEST_INT); }
	void MemberFuncStructPtrPtr3(StructParam** s, int i, int i2) { ASSERT_TRUE((*s)->val == TEST_INT); }
	void MemberFuncStructConstPtr3(const StructParam* s, int i, int i2) { ASSERT_TRUE(s->val == TEST_INT); }
	void MemberFuncStructRef3(StructParam& s, int i, int i2) { ASSERT_TRUE(s.val == TEST_INT); }
	void MemberFuncStructConstRef3(const StructParam& s, int i, int i2) { ASSERT_TRUE(s.val == TEST_INT); }

	static void StaticFuncInt3(int i, int i2, int i3) { ASSERT_TRUE(i == TEST_INT); }
	static void StaticFuncStruct3(StructParam s, int i, int i2) { ASSERT_TRUE(s.val == TEST_INT); }
	static void StaticFuncStructPtr3(StructParam* s, int i, int i2) { ASSERT_TRUE(s->val == TEST_INT); }
	static void StaticFuncStructConstPtr3(const StructParam* s, int i, int i2) { ASSERT_TRUE(s->val == TEST_INT); }
	static void StaticFuncStructRef3(StructParam& s, int i, int i2) { ASSERT_TRUE(s.val == TEST_INT); }
	static void StaticFuncStructConstRef3(const StructParam& s, int i, int i2) { ASSERT_TRUE(s.val == TEST_INT); }
};

class TestClass4
{
public:
	void MemberFuncInt4(int i, int i2, int i3, int i4) { ASSERT_TRUE(i == TEST_INT); }
	void MemberFuncInt4Const(int i, int i2, int i3, int i4) const { ASSERT_TRUE(i == TEST_INT); }
	int MemberFuncIntWithReturn4(int i, int i2, int i3, int i4) { ASSERT_TRUE(i == TEST_INT); return i; }
	void MemberFuncStruct4(StructParam s, int i, int i2, int i3) { ASSERT_TRUE(s.val == TEST_INT); }
	void MemberFuncStructPtr4(StructParam* s, int i, int i2, int i3) { ASSERT_TRUE(s->val == TEST_INT); }
	void MemberFuncStructPtrPtr4(StructParam** s, int i, int i2, int i3) { ASSERT_TRUE((*s)->val == TEST_INT); }
	void MemberFuncStructConstPtr4(const StructParam* s, int i, int i2, int i3) { ASSERT_TRUE(s->val == TEST_INT); }
	void MemberFuncStructRef4(StructParam& s, int i, int i2, int i3) { ASSERT_TRUE(s.val == TEST_INT); }
	void MemberFuncStructConstRef4(const StructParam& s, int i, int i2, int i3) { ASSERT_TRUE(s.val == TEST_INT); }

	static void StaticFuncInt4(int i, int i2, int i3, int i4) { ASSERT_TRUE(i == TEST_INT); }
	static void StaticFuncStruct4(StructParam s, int i, int i2, int i3) { ASSERT_TRUE(s.val == TEST_INT); }
	static void StaticFuncStructPtr4(StructParam* s, int i, int i2, int i3) { ASSERT_TRUE(s->val == TEST_INT); }
	static void StaticFuncStructConstPtr4(const StructParam* s, int i, int i2, int i3) { ASSERT_TRUE(s->val == TEST_INT); }
	static void StaticFuncStructRef4(StructParam& s, int i, int i2, int i3) { ASSERT_TRUE(s.val == TEST_INT); }
	static void StaticFuncStructConstRef4(const StructParam& s, int i, int i2, int i3) { ASSERT_TRUE(s.val == TEST_INT); }
};

class TestClass5
{
public:
	void MemberFuncInt5(int i, int i2, int i3, int i4, int i5) { ASSERT_TRUE(i == TEST_INT); }
	void MemberFuncInt5Const(int i, int i2, int i3, int i4, int i5) const { ASSERT_TRUE(i == TEST_INT); }
	int MemberFuncIntWithReturn5(int i, int i2, int i3, int i4, int i5) { ASSERT_TRUE(i == TEST_INT); return i; }
	void MemberFuncStruct5(StructParam s, int i, int i2, int i3, int i4) { ASSERT_TRUE(s.val == TEST_INT); }
	void MemberFuncStructPtr5(StructParam* s, int i, int i2, int i3, int i4) { ASSERT_TRUE(s->val == TEST_INT); }
	void MemberFuncStructPtrPtr5(StructParam** s, int i, int i2, int i3, int i4) { ASSERT_TRUE((*s)->val == TEST_INT); }
	void MemberFuncStructConstPtr5(const StructParam* s, int i, int i2, int i3, int i4) { ASSERT_TRUE(s->val == TEST_INT); }
	void MemberFuncStructRef5(StructParam& s, int i, int i2, int i3, int i4) { ASSERT_TRUE(s.val == TEST_INT); }
	void MemberFuncStructConstRef5(const StructParam& s, int i, int i2, int i3, int i4) { ASSERT_TRUE(s.val == TEST_INT); }

	static void StaticFuncInt5(int i, int i2, int i3, int i4, int i5) { ASSERT_TRUE(i == TEST_INT); }
	static void StaticFuncStruct5(StructParam s, int i, int i2, int i3, int i4) { ASSERT_TRUE(s.val == TEST_INT); }
	static void StaticFuncStructPtr5(StructParam* s, int i, int i2, int i3, int i4) { ASSERT_TRUE(s->val == TEST_INT); }
	static void StaticFuncStructConstPtr5(const StructParam* s, int i, int i2, int i3, int i4) { ASSERT_TRUE(s->val == TEST_INT); }
	static void StaticFuncStructRef5(StructParam& s, int i, int i2, int i3, int i4) { ASSERT_TRUE(s.val == TEST_INT); }
	static void StaticFuncStructConstRef5(const StructParam& s, int i, int i2, int i3, int i4) { ASSERT_TRUE(s.val == TEST_INT); }
};

void UnicastDelegateTests()
{
	StructParam structParam;
	structParam.val = TEST_INT;
	StructParam* pStructParam = &structParam;

	// N=0 Free Functions
	UnicastDelegate<void(void)> FreeFunc0UnicastDelegate;
	ASSERT_TRUE(FreeFunc0UnicastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFunc0UnicastDelegate);
	FreeFunc0UnicastDelegate = MakeDelegate(&FreeFunc0);
	ASSERT_TRUE(FreeFunc0UnicastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFunc0UnicastDelegate);
	FreeFunc0UnicastDelegate();
	FreeFunc0UnicastDelegate.Clear();
	ASSERT_TRUE(!FreeFunc0UnicastDelegate);

	UnicastDelegate<int(void)> FreeFuncIntWithReturn0UnicastDelegate;
	FreeFuncIntWithReturn0UnicastDelegate = MakeDelegate(&FreeFuncIntWithReturn0);
	ASSERT_TRUE(FreeFuncIntWithReturn0UnicastDelegate() == TEST_INT);

	// N=0 Member Functions
	TestClass0 testClass0;

	UnicastDelegate<void(void)> MemberFunc0UnicastDelegate;
	ASSERT_TRUE(MemberFunc0UnicastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFunc0UnicastDelegate);
	MemberFunc0UnicastDelegate = MakeDelegate(&testClass0, &TestClass0::MemberFunc0);
	ASSERT_TRUE(MemberFunc0UnicastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFunc0UnicastDelegate);
	MemberFunc0UnicastDelegate();
	MemberFunc0UnicastDelegate.Clear();
	ASSERT_TRUE(!MemberFunc0UnicastDelegate);

	UnicastDelegate<int(void)> MemberFuncIntWithReturn0UnicastDelegate;
	MemberFuncIntWithReturn0UnicastDelegate = MakeDelegate(&testClass0, &TestClass0::MemberFuncWithReturn0);
	ASSERT_TRUE(MemberFuncIntWithReturn0UnicastDelegate() == TEST_INT);

	// N=0 Static Functions
	UnicastDelegate<void(void)> StaticFunc0UnicastDelegate;
	StaticFunc0UnicastDelegate = MakeDelegate(&TestClass0::StaticFunc0);
	StaticFunc0UnicastDelegate();

	// N=1 Free Functions
	UnicastDelegate<void(int)> FreeFuncInt1UnicastDelegate;
	ASSERT_TRUE(FreeFuncInt1UnicastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFuncInt1UnicastDelegate);
	FreeFuncInt1UnicastDelegate = MakeDelegate(&FreeFuncInt1);
	ASSERT_TRUE(FreeFuncInt1UnicastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFuncInt1UnicastDelegate);
	FreeFuncInt1UnicastDelegate(TEST_INT);
	FreeFuncInt1UnicastDelegate.Clear();
	ASSERT_TRUE(!FreeFuncInt1UnicastDelegate);

	UnicastDelegate<int(int)> FreeFuncIntWithReturn1UnicastDelegate;
	FreeFuncIntWithReturn1UnicastDelegate = MakeDelegate(&FreeFuncIntWithReturn1);
	ASSERT_TRUE(FreeFuncIntWithReturn1UnicastDelegate(TEST_INT) == TEST_INT);

	UnicastDelegate<void(StructParam**)> FreeFuncPtrPtr1UnicastDelegate;
	FreeFuncPtrPtr1UnicastDelegate = MakeDelegate(&FreeFuncPtrPtr1);
	FreeFuncPtrPtr1UnicastDelegate(&pStructParam);

	UnicastDelegate<void(StructParam)> FreeFuncStruct1UnicastDelegate;
	FreeFuncStruct1UnicastDelegate = MakeDelegate(&FreeFuncStruct1);
	FreeFuncStruct1UnicastDelegate(structParam);

	UnicastDelegate<void(StructParam*)> FreeFuncStructPtr1UnicastDelegate;
	FreeFuncStructPtr1UnicastDelegate = MakeDelegate(&FreeFuncStructPtr1);
	FreeFuncStructPtr1UnicastDelegate(&structParam);

	UnicastDelegate<void(const StructParam*)> FreeFuncStructConstPtr1UnicastDelegate;
	FreeFuncStructConstPtr1UnicastDelegate = MakeDelegate(&FreeFuncStructConstPtr1);
	FreeFuncStructConstPtr1UnicastDelegate(&structParam);

	UnicastDelegate<void(StructParam&)> FreeFuncStructRef1UnicastDelegate;
	FreeFuncStructRef1UnicastDelegate = MakeDelegate(&FreeFuncStructRef1);
	FreeFuncStructRef1UnicastDelegate(structParam);

	UnicastDelegate<void(const StructParam&)> FreeFuncStructConstRef1UnicastDelegate;
	FreeFuncStructConstRef1UnicastDelegate = MakeDelegate(&FreeFuncStructConstRef1);
	FreeFuncStructConstRef1UnicastDelegate(structParam);

	// N=1 Member Functions
	TestClass1 testClass1;

	UnicastDelegate<void(int)> MemberFuncInt1UnicastDelegate;
	ASSERT_TRUE(MemberFuncInt1UnicastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFuncInt1UnicastDelegate);
	MemberFuncInt1UnicastDelegate = MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1);
	ASSERT_TRUE(MemberFuncInt1UnicastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFuncInt1UnicastDelegate);
	MemberFuncInt1UnicastDelegate(TEST_INT);
	MemberFuncInt1UnicastDelegate.Clear();
	ASSERT_TRUE(!MemberFuncInt1UnicastDelegate);

	UnicastDelegate<int(int)> MemberFuncIntWithReturn1UnicastDelegate;
	MemberFuncIntWithReturn1UnicastDelegate = MakeDelegate(&testClass1, &TestClass1::MemberFuncIntWithReturn1);
	ASSERT_TRUE(MemberFuncIntWithReturn1UnicastDelegate(TEST_INT) == TEST_INT);

	UnicastDelegate<void(StructParam)> MemberFuncStruct1UnicastDelegate;
	MemberFuncStruct1UnicastDelegate = MakeDelegate(&testClass1, &TestClass1::MemberFuncStruct1);
	MemberFuncStruct1UnicastDelegate(structParam);

	UnicastDelegate<void(StructParam*)> MemberFuncStructPtr1UnicastDelegate;
	MemberFuncStructPtr1UnicastDelegate = MakeDelegate(&testClass1, &TestClass1::MemberFuncStructPtr1);
	MemberFuncStructPtr1UnicastDelegate(&structParam);

	UnicastDelegate<void(const StructParam*)> MemberFuncStructConstPtr1UnicastDelegate;
	MemberFuncStructConstPtr1UnicastDelegate = MakeDelegate(&testClass1, &TestClass1::MemberFuncStructConstPtr1);
	MemberFuncStructConstPtr1UnicastDelegate(&structParam);

	UnicastDelegate<void(StructParam&)> MemberFuncStructRef1UnicastDelegate;
	MemberFuncStructRef1UnicastDelegate = MakeDelegate(&testClass1, &TestClass1::MemberFuncStructRef1);
	MemberFuncStructRef1UnicastDelegate(structParam);

	UnicastDelegate<void(const StructParam&)> MemberFuncStructConstRef1UnicastDelegate;
	MemberFuncStructConstRef1UnicastDelegate = MakeDelegate(&testClass1, &TestClass1::MemberFuncStructConstRef1);
	MemberFuncStructConstRef1UnicastDelegate(structParam);

	// N=1 Static Functions
	UnicastDelegate<void(int)> StaticFuncInt1UnicastDelegate;
	StaticFuncInt1UnicastDelegate = MakeDelegate(&TestClass1::StaticFuncInt1);
	StaticFuncInt1UnicastDelegate(TEST_INT);

	UnicastDelegate<void(StructParam)> StaticFuncStruct1UnicastDelegate;
	StaticFuncStruct1UnicastDelegate = MakeDelegate(&TestClass1::StaticFuncStruct1);
	StaticFuncStruct1UnicastDelegate(structParam);

	UnicastDelegate<void(StructParam*)> StaticFuncStructPtr1UnicastDelegate;
	StaticFuncStructPtr1UnicastDelegate = MakeDelegate(&TestClass1::StaticFuncStructPtr1);
	StaticFuncStructPtr1UnicastDelegate(&structParam);

	UnicastDelegate<void(const StructParam*)> StaticFuncStructConstPtr1UnicastDelegate;
	StaticFuncStructConstPtr1UnicastDelegate = MakeDelegate(&TestClass1::StaticFuncStructConstPtr1);
	StaticFuncStructConstPtr1UnicastDelegate(&structParam);

	UnicastDelegate<void(StructParam&)> StaticFuncStructRef1UnicastDelegate;
	StaticFuncStructRef1UnicastDelegate = MakeDelegate(&TestClass1::StaticFuncStructRef1);
	StaticFuncStructRef1UnicastDelegate(structParam);

	UnicastDelegate<void(const StructParam&)> StaticFuncStructConstRef1UnicastDelegate;
	StaticFuncStructConstRef1UnicastDelegate = MakeDelegate(&TestClass1::StaticFuncStructConstRef1);
	StaticFuncStructConstRef1UnicastDelegate(structParam);

	// N=2 Free Functions
	UnicastDelegate<void(int, int)> FreeFuncInt2UnicastDelegate;
	ASSERT_TRUE(FreeFuncInt2UnicastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFuncInt2UnicastDelegate);
	FreeFuncInt2UnicastDelegate = MakeDelegate(&FreeFuncInt2);
	ASSERT_TRUE(FreeFuncInt2UnicastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFuncInt2UnicastDelegate);
	FreeFuncInt2UnicastDelegate(TEST_INT, TEST_INT);
	FreeFuncInt2UnicastDelegate.Clear();
	ASSERT_TRUE(!FreeFuncInt2UnicastDelegate);

	UnicastDelegate<int(int, int)> FreeFuncIntWithReturn2UnicastDelegate;
	FreeFuncIntWithReturn2UnicastDelegate = MakeDelegate(&FreeFuncIntWithReturn2);
	ASSERT_TRUE(FreeFuncIntWithReturn2UnicastDelegate(TEST_INT, TEST_INT) == TEST_INT);

	UnicastDelegate<void(StructParam**, int)> FreeFuncPtrPtr2UnicastDelegate;
	FreeFuncPtrPtr2UnicastDelegate = MakeDelegate(&FreeFuncPtrPtr2);
	FreeFuncPtrPtr2UnicastDelegate(&pStructParam, TEST_INT);

	UnicastDelegate<void(StructParam, int)> FreeFuncStruct2UnicastDelegate;
	FreeFuncStruct2UnicastDelegate = MakeDelegate(&FreeFuncStruct2);
	FreeFuncStruct2UnicastDelegate(structParam, TEST_INT);

	UnicastDelegate<void(StructParam*, int)> FreeFuncStructPtr2UnicastDelegate;
	FreeFuncStructPtr2UnicastDelegate = MakeDelegate(&FreeFuncStructPtr2);
	FreeFuncStructPtr2UnicastDelegate(&structParam, TEST_INT);

	UnicastDelegate<void(const StructParam*, int)> FreeFuncStructConstPtr2UnicastDelegate;
	FreeFuncStructConstPtr2UnicastDelegate = MakeDelegate(&FreeFuncStructConstPtr2);
	FreeFuncStructConstPtr2UnicastDelegate(&structParam, TEST_INT);

	UnicastDelegate<void(StructParam&, int)> FreeFuncStructRef2UnicastDelegate;
	FreeFuncStructRef2UnicastDelegate = MakeDelegate(&FreeFuncStructRef2);
	FreeFuncStructRef2UnicastDelegate(structParam, TEST_INT);

	UnicastDelegate<void(const StructParam&, int)> FreeFuncStructConstRef2UnicastDelegate;
	FreeFuncStructConstRef2UnicastDelegate = MakeDelegate(&FreeFuncStructConstRef2);
	FreeFuncStructConstRef2UnicastDelegate(structParam, TEST_INT);

	// N=2 Member Functions
	TestClass2 testClass2;

	UnicastDelegate<void(int, int)> MemberFuncInt2UnicastDelegate;
	ASSERT_TRUE(MemberFuncInt2UnicastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFuncInt2UnicastDelegate);
	MemberFuncInt2UnicastDelegate = MakeDelegate(&testClass2, &TestClass2::MemberFuncInt2);
	ASSERT_TRUE(MemberFuncInt2UnicastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFuncInt2UnicastDelegate);
	MemberFuncInt2UnicastDelegate(TEST_INT, TEST_INT);
	MemberFuncInt2UnicastDelegate.Clear();
	ASSERT_TRUE(!MemberFuncInt2UnicastDelegate);

	UnicastDelegate<int(int, int)> MemberFuncIntWithReturn2UnicastDelegate;
	MemberFuncIntWithReturn2UnicastDelegate = MakeDelegate(&testClass2, &TestClass2::MemberFuncIntWithReturn2);
	ASSERT_TRUE(MemberFuncIntWithReturn2UnicastDelegate(TEST_INT, TEST_INT) == TEST_INT);

	UnicastDelegate<void(StructParam, int)> MemberFuncStruct2UnicastDelegate;
	MemberFuncStruct2UnicastDelegate = MakeDelegate(&testClass2, &TestClass2::MemberFuncStruct2);
	MemberFuncStruct2UnicastDelegate(structParam, TEST_INT);

	UnicastDelegate<void(StructParam*, int)> MemberFuncStructPtr2UnicastDelegate;
	MemberFuncStructPtr2UnicastDelegate = MakeDelegate(&testClass2, &TestClass2::MemberFuncStructPtr2);
	MemberFuncStructPtr2UnicastDelegate(&structParam, TEST_INT);

	UnicastDelegate<void(const StructParam*, int)> MemberFuncStructConstPtr2UnicastDelegate;
	MemberFuncStructConstPtr2UnicastDelegate = MakeDelegate(&testClass2, &TestClass2::MemberFuncStructConstPtr2);
	MemberFuncStructConstPtr2UnicastDelegate(&structParam, TEST_INT);

	UnicastDelegate<void(StructParam&, int)> MemberFuncStructRef2UnicastDelegate;
	MemberFuncStructRef2UnicastDelegate = MakeDelegate(&testClass2, &TestClass2::MemberFuncStructRef2);
	MemberFuncStructRef2UnicastDelegate(structParam, TEST_INT);

	UnicastDelegate<void(const StructParam&, int)> MemberFuncStructConstRef2UnicastDelegate;
	MemberFuncStructConstRef2UnicastDelegate = MakeDelegate(&testClass2, &TestClass2::MemberFuncStructConstRef2);
	MemberFuncStructConstRef2UnicastDelegate(structParam, TEST_INT);

	// N=2 Static Functions
	UnicastDelegate<void(int, int)> StaticFuncInt2UnicastDelegate;
	StaticFuncInt2UnicastDelegate = MakeDelegate(&TestClass2::StaticFuncInt2);
	StaticFuncInt2UnicastDelegate(TEST_INT, TEST_INT);

	UnicastDelegate<void(StructParam, int)> StaticFuncStruct2UnicastDelegate;
	StaticFuncStruct2UnicastDelegate = MakeDelegate(&TestClass2::StaticFuncStruct2);
	StaticFuncStruct2UnicastDelegate(structParam, TEST_INT);

	UnicastDelegate<void(StructParam*, int)> StaticFuncStructPtr2UnicastDelegate;
	StaticFuncStructPtr2UnicastDelegate = MakeDelegate(&TestClass2::StaticFuncStructPtr2);
	StaticFuncStructPtr2UnicastDelegate(&structParam, TEST_INT);

	UnicastDelegate<void(const StructParam*, int)> StaticFuncStructConstPtr2UnicastDelegate;
	StaticFuncStructConstPtr2UnicastDelegate = MakeDelegate(&TestClass2::StaticFuncStructConstPtr2);
	StaticFuncStructConstPtr2UnicastDelegate(&structParam, TEST_INT);

	UnicastDelegate<void(StructParam&, int)> StaticFuncStructRef2UnicastDelegate;
	StaticFuncStructRef2UnicastDelegate = MakeDelegate(&TestClass2::StaticFuncStructRef2);
	StaticFuncStructRef2UnicastDelegate(structParam, TEST_INT);

	UnicastDelegate<void(const StructParam&, int)> StaticFuncStructConstRef2UnicastDelegate;
	StaticFuncStructConstRef2UnicastDelegate = MakeDelegate(&TestClass2::StaticFuncStructConstRef2);
	StaticFuncStructConstRef2UnicastDelegate(structParam, TEST_INT);

}

void MulticastDelegateTests()
{
	StructParam structParam;
	structParam.val = TEST_INT;

	// N=0 Free Functions
	MulticastDelegate<void(void)> FreeFunc0MulticastDelegate;
	ASSERT_TRUE(FreeFunc0MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFunc0MulticastDelegate);
	FreeFunc0MulticastDelegate += MakeDelegate(&FreeFunc0);
	FreeFunc0MulticastDelegate += MakeDelegate(&FreeFunc0);
	ASSERT_TRUE(FreeFunc0MulticastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFunc0MulticastDelegate);
	FreeFunc0MulticastDelegate();
	FreeFunc0MulticastDelegate -= MakeDelegate(&FreeFunc0);
	ASSERT_TRUE(FreeFunc0MulticastDelegate);
	FreeFunc0MulticastDelegate.Clear();
	ASSERT_TRUE(!FreeFunc0MulticastDelegate);

	// N=0 Member Functions
	TestClass0 testClass0;

	MulticastDelegate<void(void)> MemberFunc0MulticastDelegate;
	ASSERT_TRUE(MemberFunc0MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFunc0MulticastDelegate);
	MemberFunc0MulticastDelegate += MakeDelegate(&testClass0, &TestClass0::MemberFunc0);
	ASSERT_TRUE(MemberFunc0MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFunc0MulticastDelegate);
	MemberFunc0MulticastDelegate();
	MemberFunc0MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFunc0MulticastDelegate);

	// N=0 Static Functions
	MulticastDelegate<void(void)> StaticFunc0MulticastDelegate;
	StaticFunc0MulticastDelegate += MakeDelegate(&TestClass0::StaticFunc0);
	StaticFunc0MulticastDelegate();

	// N=1 Free Functions
	MulticastDelegate<void(int)> FreeFuncInt1MulticastDelegate;
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFuncInt1MulticastDelegate);
	FreeFuncInt1MulticastDelegate += MakeDelegate(&FreeFuncInt1);
	FreeFuncInt1MulticastDelegate += MakeDelegate(&FreeFuncInt1);
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate);
	FreeFuncInt1MulticastDelegate(TEST_INT);
	FreeFuncInt1MulticastDelegate -= MakeDelegate(&FreeFuncInt1);
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate);
	FreeFuncInt1MulticastDelegate.Clear();
	ASSERT_TRUE(!FreeFuncInt1MulticastDelegate);

	MulticastDelegate<void(StructParam)> FreeFuncStruct1MulticastDelegate;
	FreeFuncStruct1MulticastDelegate += MakeDelegate(&FreeFuncStruct1);
	FreeFuncStruct1MulticastDelegate(structParam);

	MulticastDelegate<void(StructParam*)> FreeFuncStructPtr1MulticastDelegate;
	FreeFuncStructPtr1MulticastDelegate += MakeDelegate(&FreeFuncStructPtr1);
	FreeFuncStructPtr1MulticastDelegate(&structParam);

	MulticastDelegate<void(const StructParam*)> FreeFuncStructConstPtr1MulticastDelegate;
	FreeFuncStructConstPtr1MulticastDelegate += MakeDelegate(&FreeFuncStructConstPtr1);
	FreeFuncStructConstPtr1MulticastDelegate(&structParam);

	MulticastDelegate<void(StructParam&)> FreeFuncStructRef1MulticastDelegate;
	FreeFuncStructRef1MulticastDelegate += MakeDelegate(&FreeFuncStructRef1);
	FreeFuncStructRef1MulticastDelegate(structParam);

	MulticastDelegate<void(const StructParam&)> FreeFuncStructConstRef1MulticastDelegate;
	FreeFuncStructConstRef1MulticastDelegate += MakeDelegate(&FreeFuncStructConstRef1);
	FreeFuncStructConstRef1MulticastDelegate(structParam);

	// N=1 Member Functions
	TestClass1 testClass1;

	MulticastDelegate<void(int)> MemberFuncInt1MulticastDelegate;
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFuncInt1MulticastDelegate);
	MemberFuncInt1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1);
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate);
	MemberFuncInt1MulticastDelegate(TEST_INT);
	MemberFuncInt1MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFuncInt1MulticastDelegate);

	MulticastDelegate<void(StructParam)> MemberFuncStruct1MulticastDelegate;
	MemberFuncStruct1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStruct1);
	MemberFuncStruct1MulticastDelegate(structParam);

	MulticastDelegate<void(StructParam*)> MemberFuncStructPtr1MulticastDelegate;
	MemberFuncStructPtr1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructPtr1);
	MemberFuncStructPtr1MulticastDelegate(&structParam);

	MulticastDelegate<void(const StructParam*)> MemberFuncStructConstPtr1MulticastDelegate;
	MemberFuncStructConstPtr1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructConstPtr1);
	MemberFuncStructConstPtr1MulticastDelegate(&structParam);

	MulticastDelegate<void(StructParam&)> MemberFuncStructRef1MulticastDelegate;
	MemberFuncStructRef1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructRef1);
	MemberFuncStructRef1MulticastDelegate(structParam);

	MulticastDelegate<void(const StructParam&)> MemberFuncStructConstRef1MulticastDelegate;
	MemberFuncStructConstRef1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructConstRef1);
	MemberFuncStructConstRef1MulticastDelegate(structParam);

	// N=1 Static Functions
	MulticastDelegate<void(int)> StaticFuncInt1MulticastDelegate;
	StaticFuncInt1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncInt1);
	StaticFuncInt1MulticastDelegate(TEST_INT);

	MulticastDelegate<void(StructParam)> StaticFuncStruct1MulticastDelegate;
	StaticFuncStruct1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStruct1);
	StaticFuncStruct1MulticastDelegate(structParam);

	MulticastDelegate<void(StructParam*)> StaticFuncStructPtr1MulticastDelegate;
	StaticFuncStructPtr1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructPtr1);
	StaticFuncStructPtr1MulticastDelegate(&structParam);

	MulticastDelegate<void(const StructParam*)> StaticFuncStructConstPtr1MulticastDelegate;
	StaticFuncStructConstPtr1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructConstPtr1);
	StaticFuncStructConstPtr1MulticastDelegate(&structParam);

	MulticastDelegate<void(StructParam&)> StaticFuncStructRef1MulticastDelegate;
	StaticFuncStructRef1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructRef1);
	StaticFuncStructRef1MulticastDelegate(structParam);

	MulticastDelegate<void(const StructParam&)> StaticFuncStructConstRef1MulticastDelegate;
	StaticFuncStructConstRef1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructConstRef1);
	StaticFuncStructConstRef1MulticastDelegate(structParam);

	// N=2 Free Functions
	MulticastDelegate<void(int, int)> FreeFuncInt2MulticastDelegate;
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFuncInt2MulticastDelegate);
	FreeFuncInt2MulticastDelegate += MakeDelegate(&FreeFuncInt2);
	FreeFuncInt2MulticastDelegate += MakeDelegate(&FreeFuncInt2);
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate);
	FreeFuncInt2MulticastDelegate(TEST_INT, TEST_INT);
	FreeFuncInt2MulticastDelegate -= MakeDelegate(&FreeFuncInt2);
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate);
	FreeFuncInt2MulticastDelegate.Clear();
	ASSERT_TRUE(!FreeFuncInt2MulticastDelegate);

	MulticastDelegate<void(StructParam, int)> FreeFuncStruct2MulticastDelegate;
	FreeFuncStruct2MulticastDelegate += MakeDelegate(&FreeFuncStruct2);
	FreeFuncStruct2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegate<void(StructParam*, int)> FreeFuncStructPtr2MulticastDelegate;
	FreeFuncStructPtr2MulticastDelegate += MakeDelegate(&FreeFuncStructPtr2);
	FreeFuncStructPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegate<void(const StructParam*, int)> FreeFuncStructConstPtr2MulticastDelegate;
	FreeFuncStructConstPtr2MulticastDelegate += MakeDelegate(&FreeFuncStructConstPtr2);
	FreeFuncStructConstPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegate<void(StructParam&, int)> FreeFuncStructRef2MulticastDelegate;
	FreeFuncStructRef2MulticastDelegate += MakeDelegate(&FreeFuncStructRef2);
	FreeFuncStructRef2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegate<void(const StructParam&, int)> FreeFuncStructConstRef2MulticastDelegate;
	FreeFuncStructConstRef2MulticastDelegate += MakeDelegate(&FreeFuncStructConstRef2);
	FreeFuncStructConstRef2MulticastDelegate(structParam, TEST_INT);

	// N=2 Member Functions
	TestClass2 testClass2;

	MulticastDelegate<void(int, int)> MemberFuncInt2MulticastDelegate;
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFuncInt2MulticastDelegate);
	MemberFuncInt2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncInt2);
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate);
	MemberFuncInt2MulticastDelegate(TEST_INT, TEST_INT);
	MemberFuncInt2MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFuncInt2MulticastDelegate);

	MulticastDelegate<void(StructParam, int)> MemberFuncStruct2MulticastDelegate;
	MemberFuncStruct2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStruct2);
	MemberFuncStruct2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegate<void(StructParam*, int)> MemberFuncStructPtr2MulticastDelegate;
	MemberFuncStructPtr2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructPtr2);
	MemberFuncStructPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegate<void(const StructParam*, int)> MemberFuncStructConstPtr2MulticastDelegate;
	MemberFuncStructConstPtr2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructConstPtr2);
	MemberFuncStructConstPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegate<void(StructParam&, int)> MemberFuncStructRef2MulticastDelegate;
	MemberFuncStructRef2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructRef2);
	MemberFuncStructRef2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegate<void(const StructParam&, int)> MemberFuncStructConstRef2MulticastDelegate;
	MemberFuncStructConstRef2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructConstRef2);
	MemberFuncStructConstRef2MulticastDelegate(structParam, TEST_INT);

	// N=2 Static Functions
	MulticastDelegate<void(int, int)> StaticFuncInt2MulticastDelegate;
	StaticFuncInt2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncInt2);
	StaticFuncInt2MulticastDelegate(TEST_INT, TEST_INT);

	MulticastDelegate<void(StructParam, int)> StaticFuncStruct2MulticastDelegate;
	StaticFuncStruct2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStruct2);
	StaticFuncStruct2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegate<void(StructParam*, int)> StaticFuncStructPtr2MulticastDelegate;
	StaticFuncStructPtr2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructPtr2);
	StaticFuncStructPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegate<void(const StructParam*, int)> StaticFuncStructConstPtr2MulticastDelegate;
	StaticFuncStructConstPtr2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructConstPtr2);
	StaticFuncStructConstPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegate<void(StructParam&, int)> StaticFuncStructRef2MulticastDelegate;
	StaticFuncStructRef2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructRef2);
	StaticFuncStructRef2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegate<void(const StructParam&, int)> StaticFuncStructConstRef2MulticastDelegate;
	StaticFuncStructConstRef2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructConstRef2);
	StaticFuncStructConstRef2MulticastDelegate(structParam, TEST_INT);

}

// Synchronous test of MulticastDelegateSafe<>
void MulticastDelegateSafeTests()
{
	StructParam structParam;
	structParam.val = TEST_INT;

	// N=0 Member Functions
	TestClass0 testClass0;

	MulticastDelegateSafe<void(void)> MemberFunc0MulticastDelegate;
	ASSERT_TRUE(MemberFunc0MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFunc0MulticastDelegate);
	MemberFunc0MulticastDelegate += MakeDelegate(&testClass0, &TestClass0::MemberFunc0);
	ASSERT_TRUE(MemberFunc0MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFunc0MulticastDelegate);
	MemberFunc0MulticastDelegate();
	MemberFunc0MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFunc0MulticastDelegate);

	// N=1 Member Functions
	TestClass1 testClass1;

	MulticastDelegateSafe<void(int)> MemberFuncInt1MulticastDelegate;
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFuncInt1MulticastDelegate);
	MemberFuncInt1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1);
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate);
	MemberFuncInt1MulticastDelegate(TEST_INT);
	MemberFuncInt1MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFuncInt1MulticastDelegate);

	// N=2 Member Functions
	TestClass2 testClass2;

	MulticastDelegateSafe<void(int, int)> MemberFuncInt2MulticastDelegate;
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFuncInt2MulticastDelegate);
	MemberFuncInt2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncInt2);
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate);
	MemberFuncInt2MulticastDelegate(TEST_INT, TEST_INT);
	MemberFuncInt2MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFuncInt2MulticastDelegate);

}

// Asynchronous test of MulticastDelegateSafe<>
void MulticastDelegateSafeAsyncTests()
{
	StructParam structParam;
	structParam.val = TEST_INT;

	// N=0 Free Functions
	MulticastDelegateSafe<void(void)> FreeFunc0MulticastDelegate;
	ASSERT_TRUE(FreeFunc0MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFunc0MulticastDelegate);
	FreeFunc0MulticastDelegate += MakeDelegate(&FreeFunc0, testThread);
	FreeFunc0MulticastDelegate += MakeDelegate(&FreeFunc0, testThread);
	ASSERT_TRUE(FreeFunc0MulticastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFunc0MulticastDelegate);
	FreeFunc0MulticastDelegate();
	FreeFunc0MulticastDelegate -= MakeDelegate(&FreeFunc0, testThread);
	ASSERT_TRUE(FreeFunc0MulticastDelegate);
	FreeFunc0MulticastDelegate.Clear();
	ASSERT_TRUE(!FreeFunc0MulticastDelegate);

	// N=0 Member Functions
	TestClass0 testClass0;

	MulticastDelegateSafe<void(void)> MemberFunc0MulticastDelegate;
	ASSERT_TRUE(MemberFunc0MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFunc0MulticastDelegate);
	MemberFunc0MulticastDelegate += MakeDelegate(&testClass0, &TestClass0::MemberFunc0, testThread);
	MemberFunc0MulticastDelegate += MakeDelegate(&testClass0, &TestClass0::MemberFunc0Const, testThread);
	ASSERT_TRUE(MemberFunc0MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFunc0MulticastDelegate);
	if (MemberFunc0MulticastDelegate)
		MemberFunc0MulticastDelegate();
	MemberFunc0MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFunc0MulticastDelegate);

	// N=0 Static Functions
	MulticastDelegateSafe<void(void)> StaticFunc0MulticastDelegate;
	StaticFunc0MulticastDelegate += MakeDelegate(&TestClass0::StaticFunc0, testThread);
	StaticFunc0MulticastDelegate();

	// N=1 Free Functions
	MulticastDelegateSafe<void(int)> FreeFuncInt1MulticastDelegate;
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFuncInt1MulticastDelegate);
	FreeFuncInt1MulticastDelegate += MakeDelegate(&FreeFuncInt1, testThread);
	FreeFuncInt1MulticastDelegate += MakeDelegate(&FreeFuncInt1, testThread);
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate);
	FreeFuncInt1MulticastDelegate(TEST_INT);
	FreeFuncInt1MulticastDelegate -= MakeDelegate(&FreeFuncInt1, testThread);
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate);
	FreeFuncInt1MulticastDelegate.Clear();
	ASSERT_TRUE(!FreeFuncInt1MulticastDelegate);

	MulticastDelegateSafe<void(StructParam)> FreeFuncStruct1MulticastDelegate;
	FreeFuncStruct1MulticastDelegate += MakeDelegate(&FreeFuncStruct1);
	FreeFuncStruct1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(StructParam*)> FreeFuncStructPtr1MulticastDelegate;
	FreeFuncStructPtr1MulticastDelegate += MakeDelegate(&FreeFuncStructPtr1, testThread);
	FreeFuncStructPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(const StructParam*)> FreeFuncStructConstPtr1MulticastDelegate;
	FreeFuncStructConstPtr1MulticastDelegate += MakeDelegate(&FreeFuncStructConstPtr1, testThread);
	FreeFuncStructConstPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(StructParam&)> FreeFuncStructRef1MulticastDelegate;
	FreeFuncStructRef1MulticastDelegate += MakeDelegate(&FreeFuncStructRef1, testThread);
	FreeFuncStructRef1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(const StructParam&)> FreeFuncStructConstRef1MulticastDelegate;
	FreeFuncStructConstRef1MulticastDelegate += MakeDelegate(&FreeFuncStructConstRef1, testThread);
	FreeFuncStructConstRef1MulticastDelegate(structParam);

	// N=1 Member Functions
	TestClass1 testClass1;

	MulticastDelegateSafe<void(int)> MemberFuncInt1MulticastDelegate;
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFuncInt1MulticastDelegate);
	MemberFuncInt1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1, testThread);
	MemberFuncInt1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1Const, testThread);
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate);
	MemberFuncInt1MulticastDelegate(TEST_INT);
	MemberFuncInt1MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFuncInt1MulticastDelegate);

	MulticastDelegateSafe<void(StructParam)> MemberFuncStruct1MulticastDelegate;
	MemberFuncStruct1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStruct1, testThread);
	if (MemberFuncStruct1MulticastDelegate)
		MemberFuncStruct1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(StructParam*)> MemberFuncStructPtr1MulticastDelegate;
	MemberFuncStructPtr1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructPtr1, testThread);
	MemberFuncStructPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(const StructParam*)> MemberFuncStructConstPtr1MulticastDelegate;
	MemberFuncStructConstPtr1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructConstPtr1, testThread);
	MemberFuncStructConstPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(StructParam&)> MemberFuncStructRef1MulticastDelegate;
	MemberFuncStructRef1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructRef1, testThread);
	MemberFuncStructRef1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(const StructParam&)> MemberFuncStructConstRef1MulticastDelegate;
	MemberFuncStructConstRef1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructConstRef1, testThread);
	MemberFuncStructConstRef1MulticastDelegate(structParam);

	// N=1 Static Functions
	MulticastDelegateSafe<void(int)> StaticFuncInt1MulticastDelegate;
	StaticFuncInt1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncInt1, testThread);
	StaticFuncInt1MulticastDelegate(TEST_INT);

	MulticastDelegateSafe<void(StructParam)> StaticFuncStruct1MulticastDelegate;
	StaticFuncStruct1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStruct1, testThread);
	StaticFuncStruct1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(StructParam*)> StaticFuncStructPtr1MulticastDelegate;
	StaticFuncStructPtr1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructPtr1, testThread);
	StaticFuncStructPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(const StructParam*)> StaticFuncStructConstPtr1MulticastDelegate;
	StaticFuncStructConstPtr1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructConstPtr1, testThread);
	StaticFuncStructConstPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(StructParam&)> StaticFuncStructRef1MulticastDelegate;
	StaticFuncStructRef1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructRef1, testThread);
	StaticFuncStructRef1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(const StructParam&)> StaticFuncStructConstRef1MulticastDelegate;
	StaticFuncStructConstRef1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructConstRef1, testThread);
	StaticFuncStructConstRef1MulticastDelegate(structParam);

	// N=2 Free Functions
	MulticastDelegateSafe<void(int, int)> FreeFuncInt2MulticastDelegate;
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFuncInt2MulticastDelegate);
	FreeFuncInt2MulticastDelegate += MakeDelegate(&FreeFuncInt2, testThread);
	FreeFuncInt2MulticastDelegate += MakeDelegate(&FreeFuncInt2, testThread);
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate);
	FreeFuncInt2MulticastDelegate(TEST_INT, TEST_INT);
	FreeFuncInt2MulticastDelegate -= MakeDelegate(&FreeFuncInt2, testThread);
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate);
	FreeFuncInt2MulticastDelegate.Clear();
	ASSERT_TRUE(!FreeFuncInt2MulticastDelegate);

	MulticastDelegateSafe<void(StructParam, int)> FreeFuncStruct2MulticastDelegate;
	FreeFuncStruct2MulticastDelegate += MakeDelegate(&FreeFuncStruct2, testThread);
	FreeFuncStruct2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam*, int)> FreeFuncStructPtr2MulticastDelegate;
	FreeFuncStructPtr2MulticastDelegate += MakeDelegate(&FreeFuncStructPtr2, testThread);
	FreeFuncStructPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam*, int)> FreeFuncStructConstPtr2MulticastDelegate;
	FreeFuncStructConstPtr2MulticastDelegate += MakeDelegate(&FreeFuncStructConstPtr2, testThread);
	FreeFuncStructConstPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam&, int)> FreeFuncStructRef2MulticastDelegate;
	FreeFuncStructRef2MulticastDelegate += MakeDelegate(&FreeFuncStructRef2, testThread);
	FreeFuncStructRef2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam&, int)> FreeFuncStructConstRef2MulticastDelegate;
	FreeFuncStructConstRef2MulticastDelegate += MakeDelegate(&FreeFuncStructConstRef2, testThread);
	FreeFuncStructConstRef2MulticastDelegate(structParam, TEST_INT);

	// N=2 Member Functions
	TestClass2 testClass2;

	MulticastDelegateSafe<void(int, int)> MemberFuncInt2MulticastDelegate;
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFuncInt2MulticastDelegate);
	MemberFuncInt2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncInt2, testThread);
	MemberFuncInt2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncInt2Const, testThread);
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate);
	MemberFuncInt2MulticastDelegate(TEST_INT, TEST_INT);
	MemberFuncInt2MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFuncInt2MulticastDelegate);

	MulticastDelegateSafe<void(StructParam, int)> MemberFuncStruct2MulticastDelegate;
	MemberFuncStruct2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStruct2, testThread);
	if (MemberFuncStruct2MulticastDelegate)
		MemberFuncStruct2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam*, int)> MemberFuncStructPtr2MulticastDelegate;
	MemberFuncStructPtr2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructPtr2, testThread);
	MemberFuncStructPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam*, int)> MemberFuncStructConstPtr2MulticastDelegate;
	MemberFuncStructConstPtr2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructConstPtr2, testThread);
	MemberFuncStructConstPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam&, int)> MemberFuncStructRef2MulticastDelegate;
	MemberFuncStructRef2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructRef2, testThread);
	MemberFuncStructRef2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam&, int)> MemberFuncStructConstRef2MulticastDelegate;
	MemberFuncStructConstRef2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructConstRef2, testThread);
	MemberFuncStructConstRef2MulticastDelegate(structParam, TEST_INT);

	// N=2 Static Functions
	MulticastDelegateSafe<void(int, int)> StaticFuncInt2MulticastDelegate;
	StaticFuncInt2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncInt2, testThread);
	StaticFuncInt2MulticastDelegate(TEST_INT, TEST_INT);

	MulticastDelegateSafe<void(StructParam, int)> StaticFuncStruct2MulticastDelegate;
	StaticFuncStruct2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStruct2, testThread);
	StaticFuncStruct2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam*, int)> StaticFuncStructPtr2MulticastDelegate;
	StaticFuncStructPtr2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructPtr2, testThread);
	StaticFuncStructPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam*, int)> StaticFuncStructConstPtr2MulticastDelegate;
	StaticFuncStructConstPtr2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructConstPtr2, testThread);
	StaticFuncStructConstPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam&, int)> StaticFuncStructRef2MulticastDelegate;
	StaticFuncStructRef2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructRef2, testThread);
	StaticFuncStructRef2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam&, int)> StaticFuncStructConstRef2MulticastDelegate;
	StaticFuncStructConstRef2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructConstRef2, testThread);
	StaticFuncStructConstRef2MulticastDelegate(structParam, TEST_INT);

}

void DelegateMemberSpTests()
{
	std::shared_ptr<TestClass0> testClass0(new TestClass0());
	auto DelegateMemberSp0 = MakeDelegate(testClass0, &TestClass0::MemberFunc0);
	DelegateMemberSp0();

	std::shared_ptr<TestClass1> testClass1(new TestClass1());
	auto DelegateMemberSp1 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1);
	DelegateMemberSp1(TEST_INT);

	std::shared_ptr<TestClass2> testClass2(new TestClass2());
	auto DelegateMemberSp2 = MakeDelegate(testClass2, &TestClass2::MemberFuncInt2);
	DelegateMemberSp2(TEST_INT, TEST_INT);

	std::shared_ptr<TestClass3> testClass3(new TestClass3());
	auto DelegateMemberSp3 = MakeDelegate(testClass3, &TestClass3::MemberFuncInt3);
	DelegateMemberSp3(TEST_INT, TEST_INT, TEST_INT);

	std::shared_ptr<TestClass4> testClass4(new TestClass4());
	auto DelegateMemberSp4 = MakeDelegate(testClass4, &TestClass4::MemberFuncInt4);
	DelegateMemberSp4(TEST_INT, TEST_INT, TEST_INT, TEST_INT);

	std::shared_ptr<TestClass5> testClass5(new TestClass5());
	auto DelegateMemberSp5 = MakeDelegate(testClass5, &TestClass5::MemberFuncInt5);
	DelegateMemberSp5(TEST_INT, TEST_INT, TEST_INT, TEST_INT, TEST_INT);
}

void DelegateMemberSpAsyncTests()
{
	std::shared_ptr<TestClass0> testClass0(new TestClass0());
	auto DelegateMemberAsyncSp0 = MakeDelegate(testClass0, &TestClass0::MemberFunc0, testThread);
	DelegateMemberAsyncSp0();

	std::shared_ptr<TestClass1> testClass1(new TestClass1());
	auto DelegateMemberAsyncSp1 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1, testThread);
	DelegateMemberAsyncSp1(TEST_INT);

	std::shared_ptr<TestClass2> testClass2(new TestClass2());
	auto DelegateMemberAsyncSp2 = MakeDelegate(testClass2, &TestClass2::MemberFuncInt2, testThread);
	DelegateMemberAsyncSp2(TEST_INT, TEST_INT);

	std::shared_ptr<TestClass3> testClass3(new TestClass3());
	auto DelegateMemberAsyncSp3 = MakeDelegate(testClass3, &TestClass3::MemberFuncInt3, testThread);
	DelegateMemberAsyncSp3(TEST_INT, TEST_INT, TEST_INT);

	std::shared_ptr<TestClass4> testClass4(new TestClass4());
	auto DelegateMemberAsyncSp4 = MakeDelegate(testClass4, &TestClass4::MemberFuncInt4, testThread);
	DelegateMemberAsyncSp4(TEST_INT, TEST_INT, TEST_INT, TEST_INT);

	std::shared_ptr<TestClass5> testClass5(new TestClass5());
	auto DelegateMemberAsyncSp5 = MakeDelegate(testClass5, &TestClass5::MemberFuncInt5, testThread);
	DelegateMemberAsyncSp5(TEST_INT, TEST_INT, TEST_INT, TEST_INT, TEST_INT);
}

void DelegateMemberSpAsyncWaitTests()
{
	std::shared_ptr<TestClass0> testClass0(new TestClass0());
	auto DelegateMemberAsyncSp0 = MakeDelegate(testClass0, &TestClass0::MemberFunc0, testThread, WAIT_INFINITE);
	DelegateMemberAsyncSp0();

	std::shared_ptr<TestClass1> testClass1(new TestClass1());
	auto DelegateMemberAsyncSp1 = MakeDelegate(testClass1, &TestClass1::MemberFuncInt1, testThread, WAIT_INFINITE);
	DelegateMemberAsyncSp1(TEST_INT);

	std::shared_ptr<TestClass2> testClass2(new TestClass2());
	auto DelegateMemberAsyncSp2 = MakeDelegate(testClass2, &TestClass2::MemberFuncInt2, testThread, WAIT_INFINITE);
	DelegateMemberAsyncSp2(TEST_INT, TEST_INT);

	std::shared_ptr<TestClass3> testClass3(new TestClass3());
	auto DelegateMemberAsyncSp3 = MakeDelegate(testClass3, &TestClass3::MemberFuncInt3, testThread, WAIT_INFINITE);
	DelegateMemberAsyncSp3(TEST_INT, TEST_INT, TEST_INT);

	std::shared_ptr<TestClass4> testClass4(new TestClass4());
	auto DelegateMemberAsyncSp4 = MakeDelegate(testClass4, &TestClass4::MemberFuncInt4, testThread, WAIT_INFINITE);
	DelegateMemberAsyncSp4(TEST_INT, TEST_INT, TEST_INT, TEST_INT);

	std::shared_ptr<TestClass5> testClass5(new TestClass5());
	auto DelegateMemberAsyncSp5 = MakeDelegate(testClass5, &TestClass5::MemberFuncInt5, testThread, WAIT_INFINITE);
	DelegateMemberAsyncSp5(TEST_INT, TEST_INT, TEST_INT, TEST_INT, TEST_INT);
}

void DelegateMemberAsyncWaitTests()
{
	const int LOOP_CNT = 100;
	StructParam structParam;
	structParam.val = TEST_INT;
	StructParam* pStructParam = &structParam;

	// N=0 Free Functions
	MulticastDelegateSafe<void(void)> FreeFunc0MulticastDelegate;
	ASSERT_TRUE(FreeFunc0MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFunc0MulticastDelegate);
	FreeFunc0MulticastDelegate += MakeDelegate(&FreeFunc0, testThread, WAIT_INFINITE);
	FreeFunc0MulticastDelegate += MakeDelegate(&FreeFunc0, testThread, WAIT_INFINITE);
	ASSERT_TRUE(FreeFunc0MulticastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFunc0MulticastDelegate);
	FreeFunc0MulticastDelegate();
	FreeFunc0MulticastDelegate -= MakeDelegate(&FreeFunc0, testThread, WAIT_INFINITE);
	ASSERT_TRUE(FreeFunc0MulticastDelegate);
	FreeFunc0MulticastDelegate.Clear();
	ASSERT_TRUE(!FreeFunc0MulticastDelegate);

	// N=0 Member Functions
	TestClass0 testClass0;

	MulticastDelegateSafe<void(void)> MemberFunc0MulticastDelegate;
	ASSERT_TRUE(MemberFunc0MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFunc0MulticastDelegate);
	//auto d1 = MakeDelegate(&testClass0, &TestClass0::MemberFunc0, testThread, WAIT_INFINITE);
	//MemberFunc0MulticastDelegate += d1;
	MemberFunc0MulticastDelegate += MakeDelegate(&testClass0, &TestClass0::MemberFunc0, testThread, WAIT_INFINITE);
	MemberFunc0MulticastDelegate += MakeDelegate(&testClass0, &TestClass0::MemberFunc0Const, testThread, WAIT_INFINITE);
	ASSERT_TRUE(MemberFunc0MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFunc0MulticastDelegate);
	if (MemberFunc0MulticastDelegate)
		MemberFunc0MulticastDelegate();
	MemberFunc0MulticastDelegate -= MakeDelegate(&testClass0, &TestClass0::MemberFunc0, testThread, WAIT_INFINITE);
	ASSERT_TRUE(MemberFunc0MulticastDelegate.Size() == 1);
	MemberFunc0MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFunc0MulticastDelegate);

	// N=0 Static Functions
	MulticastDelegateSafe<void(void)> StaticFunc0MulticastDelegate;
	StaticFunc0MulticastDelegate += MakeDelegate(&TestClass0::StaticFunc0, testThread, WAIT_INFINITE);
	StaticFunc0MulticastDelegate();

	// N=0 Free/Member Functions with Return
	auto FreeFuncIntWithReturn0Delegate = MakeDelegate(&FreeFuncIntWithReturn0, testThread, WAIT_INFINITE);
	ASSERT_TRUE(FreeFuncIntWithReturn0Delegate);
	if (FreeFuncIntWithReturn0Delegate) {
		ASSERT_TRUE(FreeFuncIntWithReturn0Delegate() == TEST_INT);
		ASSERT_TRUE(FreeFuncIntWithReturn0Delegate.IsSuccess() == true);
		FreeFuncIntWithReturn0Delegate.Clear();
	}

	auto MemberFuncIntWithReturn0Delegate = MakeDelegate(&testClass0, &TestClass0::MemberFuncWithReturn0, testThread, WAIT_INFINITE);
	ASSERT_TRUE(MemberFuncIntWithReturn0Delegate);
	if (MemberFuncIntWithReturn0Delegate) {
		ASSERT_TRUE(MemberFuncIntWithReturn0Delegate() == TEST_INT);
		ASSERT_TRUE(MemberFuncIntWithReturn0Delegate.IsSuccess() == true);
		MemberFuncIntWithReturn0Delegate.Clear();
	}

	FreeFuncIntWithReturn0Delegate = MakeDelegate(&FreeFuncIntWithReturn0, testThread, std::chrono::milliseconds(1));
	for (int i = 0; i < LOOP_CNT; i++)
		int ret = FreeFuncIntWithReturn0Delegate();

	MemberFuncIntWithReturn0Delegate = MakeDelegate(&testClass0, &TestClass0::MemberFuncWithReturn0, testThread, std::chrono::milliseconds(1));
	for (int i = 0; i < LOOP_CNT; i++)
		int ret = MemberFuncIntWithReturn0Delegate();

	// N=1 Free Functions
	MulticastDelegateSafe<void(int)> FreeFuncInt1MulticastDelegate;
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFuncInt1MulticastDelegate);
	FreeFuncInt1MulticastDelegate += MakeDelegate(&FreeFuncInt1, testThread, WAIT_INFINITE);
	FreeFuncInt1MulticastDelegate += MakeDelegate(&FreeFuncInt1, testThread, WAIT_INFINITE);
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate);
	FreeFuncInt1MulticastDelegate(TEST_INT);
	FreeFuncInt1MulticastDelegate -= MakeDelegate(&FreeFuncInt1, testThread, WAIT_INFINITE);
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate.Size() == 1);
	ASSERT_TRUE(FreeFuncInt1MulticastDelegate);
	FreeFuncInt1MulticastDelegate.Clear();
	ASSERT_TRUE(!FreeFuncInt1MulticastDelegate);

	MulticastDelegateSafe<void(StructParam)> FreeFuncStruct1MulticastDelegate;
	FreeFuncStruct1MulticastDelegate += MakeDelegate(&FreeFuncStruct1);
	FreeFuncStruct1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(StructParam*)> FreeFuncStructPtr1MulticastDelegate;
	FreeFuncStructPtr1MulticastDelegate += MakeDelegate(&FreeFuncStructPtr1, testThread, WAIT_INFINITE);
	FreeFuncStructPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(const StructParam*)> FreeFuncStructConstPtr1MulticastDelegate;
	FreeFuncStructConstPtr1MulticastDelegate += MakeDelegate(&FreeFuncStructConstPtr1, testThread, WAIT_INFINITE);
	FreeFuncStructConstPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(StructParam&)> FreeFuncStructRef1MulticastDelegate;
	FreeFuncStructRef1MulticastDelegate += MakeDelegate(&FreeFuncStructRef1, testThread, WAIT_INFINITE);
	FreeFuncStructRef1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(const StructParam&)> FreeFuncStructConstRef1MulticastDelegate;
	FreeFuncStructConstRef1MulticastDelegate += MakeDelegate(&FreeFuncStructConstRef1, testThread, WAIT_INFINITE);
	FreeFuncStructConstRef1MulticastDelegate(structParam);

	// N=1 Member Functions
	TestClass1 testClass1;

	MulticastDelegateSafe<void(int)> MemberFuncInt1MulticastDelegate;
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFuncInt1MulticastDelegate);
	MemberFuncInt1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1, testThread, WAIT_INFINITE);
	MemberFuncInt1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncInt1Const, testThread, WAIT_INFINITE);
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFuncInt1MulticastDelegate);
	MemberFuncInt1MulticastDelegate(TEST_INT);
	MemberFuncInt1MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFuncInt1MulticastDelegate);

	MulticastDelegateSafe<void(StructParam)> MemberFuncStruct1MulticastDelegate;
	MemberFuncStruct1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStruct1, testThread, WAIT_INFINITE);
	if (MemberFuncStruct1MulticastDelegate)
		MemberFuncStruct1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(StructParam*)> MemberFuncStructPtr1MulticastDelegate;
	MemberFuncStructPtr1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructPtr1, testThread, WAIT_INFINITE);
	MemberFuncStructPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(StructParam**)> MemberFuncStructPtrPtr1MulticastDelegate;
	MemberFuncStructPtrPtr1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructPtrPtr1, testThread, WAIT_INFINITE);
	MemberFuncStructPtrPtr1MulticastDelegate(&pStructParam);

	MulticastDelegateSafe<void(const StructParam*)> MemberFuncStructConstPtr1MulticastDelegate;
	MemberFuncStructConstPtr1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructConstPtr1, testThread, WAIT_INFINITE);
	MemberFuncStructConstPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(StructParam&)> MemberFuncStructRef1MulticastDelegate;
	MemberFuncStructRef1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructRef1, testThread, WAIT_INFINITE);
	MemberFuncStructRef1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(const StructParam&)> MemberFuncStructConstRef1MulticastDelegate;
	MemberFuncStructConstRef1MulticastDelegate += MakeDelegate(&testClass1, &TestClass1::MemberFuncStructConstRef1, testThread, WAIT_INFINITE);
	MemberFuncStructConstRef1MulticastDelegate(structParam);

	// N=1 Static Functions
	MulticastDelegateSafe<void(int)> StaticFuncInt1MulticastDelegate;
	StaticFuncInt1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncInt1, testThread, WAIT_INFINITE);
	StaticFuncInt1MulticastDelegate(TEST_INT);

	MulticastDelegateSafe<void(StructParam)> StaticFuncStruct1MulticastDelegate;
	StaticFuncStruct1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStruct1, testThread, WAIT_INFINITE);
	StaticFuncStruct1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(StructParam*)> StaticFuncStructPtr1MulticastDelegate;
	StaticFuncStructPtr1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructPtr1, testThread, WAIT_INFINITE);
	StaticFuncStructPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(const StructParam*)> StaticFuncStructConstPtr1MulticastDelegate;
	StaticFuncStructConstPtr1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructConstPtr1, testThread, WAIT_INFINITE);
	StaticFuncStructConstPtr1MulticastDelegate(&structParam);

	MulticastDelegateSafe<void(StructParam&)> StaticFuncStructRef1MulticastDelegate;
	StaticFuncStructRef1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructRef1, testThread, WAIT_INFINITE);
	StaticFuncStructRef1MulticastDelegate(structParam);

	MulticastDelegateSafe<void(const StructParam&)> StaticFuncStructConstRef1MulticastDelegate;
	StaticFuncStructConstRef1MulticastDelegate += MakeDelegate(&TestClass1::StaticFuncStructConstRef1, testThread, WAIT_INFINITE);
	StaticFuncStructConstRef1MulticastDelegate(structParam);

	// N=1 Free/Member Functions with Return
	auto FreeFuncIntWithReturn1Delegate = MakeDelegate(&FreeFuncIntWithReturn1, testThread, WAIT_INFINITE);
	ASSERT_TRUE(FreeFuncIntWithReturn1Delegate);
	if (FreeFuncIntWithReturn1Delegate) {
		ASSERT_TRUE(FreeFuncIntWithReturn1Delegate(TEST_INT) == TEST_INT);
		ASSERT_TRUE(FreeFuncIntWithReturn1Delegate.IsSuccess() == true);
		FreeFuncIntWithReturn1Delegate.Clear();
	}

	auto MemberFuncIntWithReturn1Delegate = MakeDelegate(&testClass1, &TestClass1::MemberFuncIntWithReturn1, testThread, WAIT_INFINITE);
	ASSERT_TRUE(MemberFuncIntWithReturn1Delegate);
	if (MemberFuncIntWithReturn1Delegate) {
		ASSERT_TRUE(MemberFuncIntWithReturn1Delegate(TEST_INT) == TEST_INT);
		ASSERT_TRUE(MemberFuncIntWithReturn1Delegate.IsSuccess() == true);
		MemberFuncIntWithReturn1Delegate.Clear();
	}

	FreeFuncIntWithReturn1Delegate = MakeDelegate(&FreeFuncIntWithReturn1, testThread, std::chrono::milliseconds(1));
	for (int i = 0; i < LOOP_CNT; i++)
		int ret = FreeFuncIntWithReturn1Delegate(TEST_INT);

	MemberFuncIntWithReturn1Delegate = MakeDelegate(&testClass1, &TestClass1::MemberFuncIntWithReturn1, testThread, std::chrono::milliseconds(1));
	for (int i = 0; i < LOOP_CNT; i++)
		int ret = MemberFuncIntWithReturn1Delegate(TEST_INT);

	// N=2 Free Functions
	MulticastDelegateSafe<void(int, int)> FreeFuncInt2MulticastDelegate;
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!FreeFuncInt2MulticastDelegate);
	FreeFuncInt2MulticastDelegate += MakeDelegate(&FreeFuncInt2, testThread, WAIT_INFINITE);
	FreeFuncInt2MulticastDelegate += MakeDelegate(&FreeFuncInt2, testThread, WAIT_INFINITE);
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate.Empty() == false);
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate);
	FreeFuncInt2MulticastDelegate(TEST_INT, TEST_INT);
	FreeFuncInt2MulticastDelegate -= MakeDelegate(&FreeFuncInt2, testThread, WAIT_INFINITE);
	ASSERT_TRUE(FreeFuncInt2MulticastDelegate);
	FreeFuncInt2MulticastDelegate.Clear();
	ASSERT_TRUE(!FreeFuncInt2MulticastDelegate);

	MulticastDelegateSafe<void(StructParam, int)> FreeFuncStruct2MulticastDelegate;
	FreeFuncStruct2MulticastDelegate += MakeDelegate(&FreeFuncStruct2, testThread, WAIT_INFINITE);
	FreeFuncStruct2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam*, int)> FreeFuncStructPtr2MulticastDelegate;
	FreeFuncStructPtr2MulticastDelegate += MakeDelegate(&FreeFuncStructPtr2, testThread, WAIT_INFINITE);
	FreeFuncStructPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam*, int)> FreeFuncStructConstPtr2MulticastDelegate;
	FreeFuncStructConstPtr2MulticastDelegate += MakeDelegate(&FreeFuncStructConstPtr2, testThread, WAIT_INFINITE);
	FreeFuncStructConstPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam&, int)> FreeFuncStructRef2MulticastDelegate;
	FreeFuncStructRef2MulticastDelegate += MakeDelegate(&FreeFuncStructRef2, testThread, WAIT_INFINITE);
	FreeFuncStructRef2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam&, int)> FreeFuncStructConstRef2MulticastDelegate;
	FreeFuncStructConstRef2MulticastDelegate += MakeDelegate(&FreeFuncStructConstRef2, testThread, WAIT_INFINITE);
	FreeFuncStructConstRef2MulticastDelegate(structParam, TEST_INT);

	// N=2 Member Functions
	TestClass2 testClass2;

	MulticastDelegateSafe<void(int, int)> MemberFuncInt2MulticastDelegate;
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate.Empty() == true);
	ASSERT_TRUE(!MemberFuncInt2MulticastDelegate);
	MemberFuncInt2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncInt2, testThread, WAIT_INFINITE);
	MemberFuncInt2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncInt2Const, testThread, WAIT_INFINITE);
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate.Empty() == false);
	ASSERT_TRUE(MemberFuncInt2MulticastDelegate);
	MemberFuncInt2MulticastDelegate(TEST_INT, TEST_INT);
	MemberFuncInt2MulticastDelegate.Clear();
	ASSERT_TRUE(!MemberFuncInt2MulticastDelegate);

	MulticastDelegateSafe<void(StructParam, int)> MemberFuncStruct2MulticastDelegate;
	MemberFuncStruct2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStruct2, testThread, WAIT_INFINITE);
	if (MemberFuncStruct2MulticastDelegate)
		MemberFuncStruct2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam*, int)> MemberFuncStructPtr2MulticastDelegate;
	MemberFuncStructPtr2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructPtr2, testThread, WAIT_INFINITE);
	MemberFuncStructPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam*, int)> MemberFuncStructConstPtr2MulticastDelegate;
	MemberFuncStructConstPtr2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructConstPtr2, testThread, WAIT_INFINITE);
	MemberFuncStructConstPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam&, int)> MemberFuncStructRef2MulticastDelegate;
	MemberFuncStructRef2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructRef2, testThread, WAIT_INFINITE);
	MemberFuncStructRef2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam&, int)> MemberFuncStructConstRef2MulticastDelegate;
	MemberFuncStructConstRef2MulticastDelegate += MakeDelegate(&testClass2, &TestClass2::MemberFuncStructConstRef2, testThread, WAIT_INFINITE);
	MemberFuncStructConstRef2MulticastDelegate(structParam, TEST_INT);

	// N=2 Static Functions
	MulticastDelegateSafe<void(int, int)> StaticFuncInt2MulticastDelegate;
	StaticFuncInt2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncInt2, testThread, WAIT_INFINITE);
	StaticFuncInt2MulticastDelegate(TEST_INT, TEST_INT);

	MulticastDelegateSafe<void(StructParam, int)> StaticFuncStruct2MulticastDelegate;
	StaticFuncStruct2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStruct2, testThread, WAIT_INFINITE);
	StaticFuncStruct2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam*, int)> StaticFuncStructPtr2MulticastDelegate;
	StaticFuncStructPtr2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructPtr2, testThread, WAIT_INFINITE);
	StaticFuncStructPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam*, int)> StaticFuncStructConstPtr2MulticastDelegate;
	StaticFuncStructConstPtr2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructConstPtr2, testThread, WAIT_INFINITE);
	StaticFuncStructConstPtr2MulticastDelegate(&structParam, TEST_INT);

	MulticastDelegateSafe<void(StructParam&, int)> StaticFuncStructRef2MulticastDelegate;
	StaticFuncStructRef2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructRef2, testThread, WAIT_INFINITE);
	StaticFuncStructRef2MulticastDelegate(structParam, TEST_INT);

	MulticastDelegateSafe<void(const StructParam&, int)> StaticFuncStructConstRef2MulticastDelegate;
	StaticFuncStructConstRef2MulticastDelegate += MakeDelegate(&TestClass2::StaticFuncStructConstRef2, testThread, WAIT_INFINITE);
	StaticFuncStructConstRef2MulticastDelegate(structParam, TEST_INT);

	// N=2 Free/Member Functions with Return
	auto FreeFuncIntWithReturn2Delegate = MakeDelegate(&FreeFuncIntWithReturn2, testThread, WAIT_INFINITE);
	ASSERT_TRUE(FreeFuncIntWithReturn2Delegate);
	if (FreeFuncIntWithReturn2Delegate) {
		ASSERT_TRUE(FreeFuncIntWithReturn2Delegate(TEST_INT, TEST_INT) == TEST_INT);
		ASSERT_TRUE(FreeFuncIntWithReturn2Delegate.IsSuccess() == true);
		FreeFuncIntWithReturn2Delegate.Clear();
	}

	auto MemberFuncIntWithReturn2Delegate = MakeDelegate(&testClass2, &TestClass2::MemberFuncIntWithReturn2, testThread, WAIT_INFINITE);
	ASSERT_TRUE(MemberFuncIntWithReturn2Delegate);
	if (MemberFuncIntWithReturn2Delegate) {
		ASSERT_TRUE(MemberFuncIntWithReturn2Delegate(TEST_INT, TEST_INT) == TEST_INT);
		ASSERT_TRUE(MemberFuncIntWithReturn2Delegate.IsSuccess() == true);
		MemberFuncIntWithReturn2Delegate.Clear();
	}

	FreeFuncIntWithReturn2Delegate = MakeDelegate(&FreeFuncIntWithReturn2, testThread, std::chrono::milliseconds(1));
	for (int i = 0; i < LOOP_CNT; i++)
		int ret = FreeFuncIntWithReturn2Delegate(TEST_INT, TEST_INT);

	MemberFuncIntWithReturn2Delegate = MakeDelegate(&testClass2, &TestClass2::MemberFuncIntWithReturn2, testThread, std::chrono::milliseconds(1));
	for (int i = 0; i < LOOP_CNT; i++)
		int ret = MemberFuncIntWithReturn2Delegate(TEST_INT, TEST_INT);

}

extern void DelegateTests();
extern void DelegateAsyncTests();
extern void DelegateAsyncWaitTests();
extern void DelegateRemoteTests();
extern void DelegateThreadsTests();
extern void ContainersTests();
extern void RemoteChannelTests();
extern void SerializeTests();
extern void DispatcherTests();
extern void MonotonicGuardTests();
extern void TimerDelegateTests();
#ifdef DMQ_ALLOCATOR
extern void AllocatorTests();
#endif
extern void MakeTupleHeapTests();

void RunDelegateUnitTests()
{
	LOG_INFO("DelegateUnitTests Begin");

	try
	{
		ContainersTests();
		DelegateTests();
		DelegateAsyncTests();
		DelegateAsyncWaitTests();
		DelegateRemoteTests();
		DelegateThreadsTests();
		RemoteChannelTests();
		SerializeTests();
		DispatcherTests();
		MonotonicGuardTests();
		TimerDelegateTests();
#ifdef DMQ_ALLOCATOR
		AllocatorTests();
#endif
		MakeTupleHeapTests();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("Unit Tests Failed: {}", e.what());
		std::cout << "Unit Tests Failed: " << e.what() << std::endl;
		ASSERT_TRUE(false);
	}
	catch (...)
	{
		LOG_ERROR("Unit Tests Failed!");
		std::cout << "Unit Tests Failed!" << std::endl;
		ASSERT_TRUE(false);
	}

	testThread.CreateThread();

#ifdef _WIN32
	LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds, TotalElapsedMicroseconds = { 0 };
	LARGE_INTEGER Frequency;

	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartingTime);
#endif

	try
	{
		// Run unit tests repeatedly to expose problems (e.g. deadlocks, memory leaks)
		// with async delegates.
		for (int i = 0; i < 100; i++)
		{
			UnicastDelegateTests();
			MulticastDelegateTests();
			MulticastDelegateSafeTests();
			MulticastDelegateSafeAsyncTests();
			DelegateMemberAsyncWaitTests();
			DelegateMemberSpTests();
			DelegateMemberSpAsyncTests();
			DelegateMemberSpAsyncWaitTests();
		}
	}
	catch(const std::exception& e)
	{
		LOG_ERROR("Unit Tests Failed: {}", e.what());
		std::cout << "Unit Tests Failed: " << e.what() << std::endl;
		ASSERT_TRUE(false);
	}
	catch (...)
	{
		LOG_ERROR("Unit Tests Failed!");
		std::cout << "Unit Tests Failed!" << std::endl;
		ASSERT_TRUE(false);
	}

#ifdef _WIN32
	QueryPerformanceCounter(&EndingTime);
	ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
	ElapsedMicroseconds.QuadPart *= 1000000;
	ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
	std::cout << "Elapsed Time: " << (float)ElapsedMicroseconds.QuadPart / 1000000.0f << " seconds" << std::endl;
#endif

	testThread.ExitThread();

	LOG_INFO("DelegateUnitTests End");
}

