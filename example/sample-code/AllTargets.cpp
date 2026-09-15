/// @file
/// @brief All target function types register with a delegate container 
/// and invoked. 

#include "AllTargets.h"
#include "DelegateMQ.h"
#include <iostream>
#include <future>
#include <chrono>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

namespace Example
{
    static Thread workerThread1("AllTargets");

    static std::atomic<int> callCnt = 0;

    static void FreeFunc(int value) {
        cout << "FreeFunc " << value << " " << ++callCnt << endl;
    }

    // Simple test invoking all target types
    static void TestAllTargetTypes() {
        class Class {
        public:
            static void StaticFunc(int value) {
                cout << "StaticFunc " << value << " " << ++callCnt << endl;
            }

            void MemberFunc(int value) {
                cout << "MemberFunc " << value << " " << ++callCnt << endl;
            }

            void MemberFuncConst(int value) const {
                cout << "MemberFuncConst " << value << " " << ++callCnt << endl;
            }

            virtual void MemberFuncVirtual(int value) {
                cout << "MemberFuncVirtual " << value << " " << ++callCnt << endl;
            }
        };

        int stackVal = 100;
        auto LambdaCapture = [stackVal](int i) {
            std::cout << "LambdaCapture " << i + stackVal << " " << ++callCnt << endl;
        };

        auto LambdaNoCapture = [](int i) {
            std::cout << "LambdaNoCapture " << i << " " << ++callCnt << endl;
        };

        auto LambdaForcedCapture = +[](int i) {
            std::cout << "LambdaForcedCapture " << i << " " << ++callCnt << endl;
        };

        Class testClass;
        std::shared_ptr<Class> testClassSp = xmake_shared<Class>();

        // Create a multicast delegate container that accepts Delegate<void(int)> delegates.
        // Any function with the signature "void Func(int)".
        MulticastDelegateSafe<void(int)> delegateA;

        // Add all callable function targets to the delegate container
        // Synchronous delegates
        delegateA += MakeDelegate(&FreeFunc);
        delegateA += MakeDelegate(LambdaCapture);
        delegateA += MakeDelegate(LambdaNoCapture);
        delegateA += MakeDelegate(LambdaForcedCapture);
        delegateA += MakeDelegate(&Class::StaticFunc);
        delegateA += MakeDelegate(&testClass, &Class::MemberFunc);
        delegateA += MakeDelegate(&testClass, &Class::MemberFuncConst);
        delegateA += MakeDelegate(testClassSp, &Class::MemberFunc);
        delegateA += MakeDelegate(testClassSp, &Class::MemberFuncConst);

        // Asynchronous delegates
        delegateA += MakeDelegate(&FreeFunc, workerThread1);
        delegateA += MakeDelegate(LambdaCapture, workerThread1);
        delegateA += MakeDelegate(LambdaNoCapture, workerThread1);
        delegateA += MakeDelegate(LambdaForcedCapture, workerThread1);
        delegateA += MakeDelegate(&Class::StaticFunc, workerThread1);
        delegateA += MakeDelegate(&testClass, &Class::MemberFunc, workerThread1);
        delegateA += MakeDelegate(&testClass, &Class::MemberFuncConst, workerThread1);
        delegateA += MakeDelegate(testClassSp, &Class::MemberFunc, workerThread1);
        delegateA += MakeDelegate(testClassSp, &Class::MemberFuncConst, workerThread1);

        // Asynchronous blocking delegates
        delegateA += MakeDelegate(&FreeFunc, workerThread1, WAIT_INFINITE);
        delegateA += MakeDelegate(LambdaCapture, workerThread1, WAIT_INFINITE);
        delegateA += MakeDelegate(LambdaNoCapture, workerThread1, WAIT_INFINITE);
        delegateA += MakeDelegate(LambdaForcedCapture, workerThread1, WAIT_INFINITE);
        delegateA += MakeDelegate(&Class::StaticFunc, workerThread1, WAIT_INFINITE);
        delegateA += MakeDelegate(&testClass, &Class::MemberFunc, workerThread1, WAIT_INFINITE);
        delegateA += MakeDelegate(&testClass, &Class::MemberFuncConst, workerThread1, WAIT_INFINITE);
        delegateA += MakeDelegate(testClassSp, &Class::MemberFunc, workerThread1, WAIT_INFINITE);
        delegateA += MakeDelegate(testClassSp, &Class::MemberFuncConst, workerThread1, WAIT_INFINITE);

        // Invoke all callable function targets stored within the delegate container
        delegateA(123);

        // Wait for async delegate invocations to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Remove all callable function targets from the delegate container
        // Synchronous delegates
        delegateA -= MakeDelegate(&FreeFunc);
        delegateA -= MakeDelegate(LambdaCapture);
        delegateA -= MakeDelegate(LambdaNoCapture);
        delegateA -= MakeDelegate(LambdaForcedCapture);
        delegateA -= MakeDelegate(&Class::StaticFunc);
        delegateA -= MakeDelegate(&testClass, &Class::MemberFunc);
        delegateA -= MakeDelegate(&testClass, &Class::MemberFuncConst);
        delegateA -= MakeDelegate(testClassSp, &Class::MemberFunc);
        delegateA -= MakeDelegate(testClassSp, &Class::MemberFuncConst);

        // Asynchronous delegates
        delegateA -= MakeDelegate(&FreeFunc, workerThread1);
        delegateA -= MakeDelegate(LambdaCapture, workerThread1);
        delegateA -= MakeDelegate(LambdaNoCapture, workerThread1);
        delegateA -= MakeDelegate(LambdaForcedCapture, workerThread1);
        delegateA -= MakeDelegate(&Class::StaticFunc, workerThread1);
        delegateA -= MakeDelegate(&testClass, &Class::MemberFunc, workerThread1);
        delegateA -= MakeDelegate(&testClass, &Class::MemberFuncConst, workerThread1);
        delegateA -= MakeDelegate(testClassSp, &Class::MemberFunc, workerThread1);
        delegateA -= MakeDelegate(testClassSp, &Class::MemberFuncConst, workerThread1);

        // Asynchronous blocking delegates
        delegateA -= MakeDelegate(&FreeFunc, workerThread1, WAIT_INFINITE);
        delegateA -= MakeDelegate(LambdaCapture, workerThread1, WAIT_INFINITE);
        delegateA -= MakeDelegate(LambdaNoCapture, workerThread1, WAIT_INFINITE);
        delegateA -= MakeDelegate(LambdaForcedCapture, workerThread1, WAIT_INFINITE);
        delegateA -= MakeDelegate(&Class::StaticFunc, workerThread1, WAIT_INFINITE);
        delegateA -= MakeDelegate(&testClass, &Class::MemberFunc, workerThread1, WAIT_INFINITE);
        delegateA -= MakeDelegate(&testClass, &Class::MemberFuncConst, workerThread1, WAIT_INFINITE);
        delegateA -= MakeDelegate(testClassSp, &Class::MemberFunc, workerThread1, WAIT_INFINITE);
        delegateA -= MakeDelegate(testClassSp, &Class::MemberFuncConst, workerThread1, WAIT_INFINITE);

        DMQ_ASSERT_TRUE(delegateA.Size() == 0);
        DMQ_ASSERT_TRUE(callCnt == 27);
    }

    void AllTargetsExample()
    {
        workerThread1.CreateThread();

        callCnt = 0;
        TestAllTargetTypes();

        workerThread1.ExitThread();
    }
}
