#pragma once

#include <iostream>
#include <functional>
#include <memory>
#include <cstdint>
#include "DelegateMQ.h"

namespace UnitTestData
{
	static int callCnt = 0;

	static const int TEST_INT = 12345678;

	static std::function<void(int)> LambdaNoCapture = [](int i) {
	};

	static std::function<void(int)> LambdaNoCapture2 = [](int i) {
	};

	static std::function<void(int)> LambdaForcedCapture = +[](int i) {
	};

	static auto LambdaUnqiue = [](int x) -> std::unique_ptr<int> {
		std::unique_ptr<int> t2{ new int };
		*t2 = x;
		return t2;
	};

	class Class {
	public:
		Class() { m_construtorCnt++; }
		Class(const Class& rhs) { m_construtorCnt++; }

		std::uint16_t Func() { return 0; }
		std::uint16_t FuncConst() const { return 0; }
		std::unique_ptr<int> FuncUnique(int i) { return std::make_unique<int>(i); }
		void FuncRvalueRef(int&& i) { DMQ_ASSERT_TRUE(i == TEST_INT); }
		void VoidPtrArgNull(void* p) { DMQ_ASSERT_TRUE(p == nullptr); }
		void VoidPtrArgNotNull(void* p) { DMQ_ASSERT_TRUE(p != nullptr); }
		void* RetVoidPtr() {
			static const char* str = "Hello World!";
			return (void*)str;
		}

		static int m_construtorCnt;
		XALLOCATOR
	};

	std::ostream& operator<<(std::ostream& os, const Class& data);
	std::istream& operator>>(std::istream& is, Class& data);

	static void ConstructorCnt(Class* c) {}
	static std::uint16_t Func() { return 0; }
	static std::unique_ptr<int> FuncUnique(int i) { return std::make_unique<int>(i); }
	static void FuncRvalueRef(int&& i) { DMQ_ASSERT_TRUE(i == TEST_INT); }
	static void VoidPtrArgNull(void* p) { DMQ_ASSERT_TRUE(p == nullptr); }
	static void VoidPtrArgNotNull(void* p) { DMQ_ASSERT_TRUE(p != nullptr); }
	static void* RetVoidPtr() { 
		static const char* str = "Hello World!";
		return (void*)str;
	}

	class Base {
	public:
		virtual int Func() { return 0; }
		XALLOCATOR
	};

	class Derive : public Base {
	public:
		virtual int Func() { return TEST_INT; }
	};

	class ClassSingleton
	{
	public:
		static ClassSingleton& GetInstance() {
			static ClassSingleton instance;
			return instance;
		}
		static std::shared_ptr<ClassSingleton> GetInstanceSp() {
			auto deleter = [](ClassSingleton*) {};
			return std::shared_ptr<ClassSingleton>(&GetInstance(), deleter);
		}
		void SetData(int data) { m_data = data; }

	private:
		ClassSingleton() = default;
		ClassSingleton(const ClassSingleton& other) = delete;
		ClassSingleton& operator=(const ClassSingleton&) = delete;
		int m_data = 0;
	};

	static void SetClassSingletonRef(ClassSingleton& s) {}
	static void SetClassSingletonPtr(ClassSingleton* s) {}
	static void SetClassSingletonPtrPtr(ClassSingleton** s) {}
	static void SetClassSingletonShared(std::shared_ptr<ClassSingleton> s) {}

	class SetClassSingleton
	{
	public:
		void Ref(ClassSingleton& s) {}
		void Ptr(ClassSingleton* s) {}
		void PtrPtr(ClassSingleton** s) {}
		void Shared(std::shared_ptr<ClassSingleton> s) {}
	};

	static void NullPtrArg(int* n) { DMQ_ASSERT_TRUE(n == nullptr); }
	static void NullPtrPtrArg(int** n) { DMQ_ASSERT_TRUE(*n == nullptr); }

	struct StructParam { int val; };

	static void OutgoingPtrArg(StructParam* s, int* i) { 
		DMQ_ASSERT_TRUE(s != nullptr);
		DMQ_ASSERT_TRUE(s->val == TEST_INT);
		s->val++; 
		(*i)++;
	}

	static void OutgoingPtrPtrArg(StructParam** s) {
		StructParam* param = new StructParam;
		param->val = TEST_INT;
		DMQ_ASSERT_TRUE(*s == nullptr);
		(*s) = param;
	}

	// Async variant: ptr-ptr output can't propagate back to caller, so no allocation
	static void OutgoingPtrPtrArgAsync(StructParam** s) {
		DMQ_ASSERT_TRUE(*s == nullptr);
	}

	static void OutgoingRefArg(StructParam& s) {
		s.val++;
	}

	static int FreeFuncIntWithReturn0() { return TEST_INT; }

	static void FreeFunc0() { }

	static void FreeFuncInt1(int i) { DMQ_ASSERT_TRUE(i == TEST_INT); }
	static void FreeFuncInt1_2(int i) { DMQ_ASSERT_TRUE(i == TEST_INT); }
	static int FreeFuncIntWithReturn1(int i) { DMQ_ASSERT_TRUE(i == TEST_INT); return i; }
	static void FreeFuncPtrPtr1(StructParam** s) { DMQ_ASSERT_TRUE((*s)->val == TEST_INT); (*s)->val++; }
	static void FreeFuncStruct1(StructParam s) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
	static void FreeFuncStructPtr1(StructParam* s) { DMQ_ASSERT_TRUE(s->val == TEST_INT); s->val++; }
	static void FreeFuncStructConstPtr1(const StructParam* s) { DMQ_ASSERT_TRUE(s->val == TEST_INT); }
	static void FreeFuncStructRef1(StructParam& s) { DMQ_ASSERT_TRUE(s.val == TEST_INT); s.val++; }
	static void FreeFuncStructConstRef1(const StructParam& s) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }

	static void FreeFuncInt2(int i, int i2) { DMQ_ASSERT_TRUE(i == TEST_INT); DMQ_ASSERT_TRUE(i2 == TEST_INT); }
	static int FreeFuncIntWithReturn2(int i, int i2) { DMQ_ASSERT_TRUE(i == TEST_INT); return i; }
	static void FreeFuncPtrPtr2(StructParam** s, int i) { DMQ_ASSERT_TRUE((*s)->val == TEST_INT); }
	static void FreeFuncStruct2(StructParam s, int i) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
	static void FreeFuncStructPtr2(StructParam* s, int i) { DMQ_ASSERT_TRUE(s->val == TEST_INT); }
	static void FreeFuncStructConstPtr2(const StructParam* s, int i) { DMQ_ASSERT_TRUE(s->val == TEST_INT); }
	static void FreeFuncStructRef2(StructParam& s, int i) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
	static void FreeFuncStructConstRef2(const StructParam& s, int i) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }

	class TestClass0
	{
	public:
		void MemberFunc0() { }
		void MemberFunc0Const() const { }
		int MemberFuncWithReturn0() { return TEST_INT; }

		static void StaticFunc0() { }
		XALLOCATOR
	};

	class TestClass1
	{
	public:
		int ConstCheck(int i) { DMQ_ASSERT_TRUE(i == TEST_INT); return TEST_INT; }
		int ConstCheck(int i) const { DMQ_ASSERT_TRUE(i == TEST_INT); return TEST_INT; }
		void FuncRvalueRef(int&& i) { DMQ_ASSERT_TRUE(i == TEST_INT); }

		void MemberFuncInt1(int i) { DMQ_ASSERT_TRUE(i == TEST_INT); }
		void MemberFuncInt1_2(int i) { DMQ_ASSERT_TRUE(i == TEST_INT); }
		void MemberFuncInt1Const(int i) const { DMQ_ASSERT_TRUE(i == TEST_INT); }
		int MemberFuncIntWithReturn1(int i) { DMQ_ASSERT_TRUE(i == TEST_INT); return i; }
		void MemberFuncStruct1(StructParam s) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
		void MemberFuncStructPtr1(StructParam* s) { DMQ_ASSERT_TRUE(s->val == TEST_INT); }
		void MemberFuncStructPtrPtr1(StructParam** s) { DMQ_ASSERT_TRUE((*s)->val == TEST_INT); }
		void MemberFuncStructConstPtr1(const StructParam* s) { DMQ_ASSERT_TRUE(s->val == TEST_INT); }
		void MemberFuncStructRef1(StructParam& s) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
		void MemberFuncStructConstRef1(const StructParam& s) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }

		static void StaticFuncInt1(int i) { DMQ_ASSERT_TRUE(i == TEST_INT); }
		static void StaticFuncStruct1(StructParam s) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
		static void StaticFuncStructPtr1(StructParam* s) { DMQ_ASSERT_TRUE(s->val == TEST_INT); }
		static void StaticFuncStructConstPtr1(const StructParam* s) { DMQ_ASSERT_TRUE(s->val == TEST_INT); }
		static void StaticFuncStructRef1(StructParam& s) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
		static void StaticFuncStructConstRef1(const StructParam& s) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
		XALLOCATOR
	};

	class TestClass2
	{
	public:
		void MemberFuncInt2(int i, int i2) { DMQ_ASSERT_TRUE(i == TEST_INT); }
		void MemberFuncInt2Const(int i, int i2) const { DMQ_ASSERT_TRUE(i == TEST_INT); }
		int MemberFuncIntWithReturn2(int i, int i2) { DMQ_ASSERT_TRUE(i == TEST_INT); return i; }
		void MemberFuncStruct2(StructParam s, int i) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
		void MemberFuncStructPtr2(StructParam* s, int i) { DMQ_ASSERT_TRUE(s->val == TEST_INT); }
		void MemberFuncStructPtrPtr2(StructParam** s, int i) { DMQ_ASSERT_TRUE((*s)->val == TEST_INT); }
		void MemberFuncStructConstPtr2(const StructParam* s, int i) { DMQ_ASSERT_TRUE(s->val == TEST_INT); }
		void MemberFuncStructRef2(StructParam& s, int i) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
		void MemberFuncStructConstRef2(const StructParam& s, int i) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }

		static void StaticFuncInt2(int i, int i2) { DMQ_ASSERT_TRUE(i == TEST_INT); }
		static void StaticFuncStruct2(StructParam s, int i) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
		static void StaticFuncStructPtr2(StructParam* s, int i) { DMQ_ASSERT_TRUE(s->val == TEST_INT); }
		static void StaticFuncStructConstPtr2(const StructParam* s, int i) { DMQ_ASSERT_TRUE(s->val == TEST_INT); }
		static void StaticFuncStructRef2(StructParam& s, int i) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
		static void StaticFuncStructConstRef2(const StructParam& s, int i) { DMQ_ASSERT_TRUE(s.val == TEST_INT); }
		XALLOCATOR
	};
}

