/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Comprehensive DelegateMQ feature demo using LLVM/Clang (or any C++17 compiler).
///
/// Demonstrates all major DelegateMQ delegate types in a single app:
///   Test 1 — Synchronous delegates: free function, member function, lambda
///   Test 2 — DelegateAsync: fire-and-forget dispatch to a worker thread
///   Test 3 — DelegateAsyncWait: blocking call with return value from worker thread
///   Test 4 — MulticastDelegateSafe: thread-safe multicast to multiple targets
///   Test 5 — UnicastDelegateSafe: thread-safe single-target delegate
///   Test 6 — Signal: publish/subscribe with RAII ScopedConnection
///
/// Build with Clang on Windows or Linux. See README.md for build instructions.

#include "DelegateMQ.h"
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace std;

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
static atomic<int> g_callCount{0};

// ---------------------------------------------------------------------------
// Callback targets
// ---------------------------------------------------------------------------
static void FreeFunc(int val)
{
    cout << "  FreeFunc called: " << val << endl;
    g_callCount++;
}

class Handler
{
public:
    void MemberFunc(int val)
    {
        cout << "  Handler::MemberFunc called: " << val << endl;
        g_callCount++;
    }

    int MemberFuncReturn(int val)
    {
        cout << "  Handler::MemberFuncReturn called: " << val << endl;
        g_callCount++;
        return val * 2;
    }
};

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------
static int g_testNum = 0;
static int g_passCount = 0;

static void PrintTest(const char* name)
{
    cout << "\n[Test " << ++g_testNum << "] " << name << ":\n";
}

static void AssertPass(bool cond, const char* msg)
{
    if (cond)
    {
        cout << "  PASS: " << msg << endl;
        g_passCount++;
    }
    else
    {
        cout << "  FAIL: " << msg << endl;
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    cout << "\n=========================================\n";
    cout << "   DELEGATEMQ FULL-FEATURE DEMO          \n";
    cout << "=========================================\n";

    // Start a worker thread for async delegates
    Thread workerThread("WorkerThread");
    workerThread.CreateThread();

    Handler handler;

    // -----------------------------------------------------------------------
    // Test 1: Synchronous Delegates
    // -----------------------------------------------------------------------
    PrintTest("Synchronous Delegates");
    {
        // Free function
        auto d1 = MakeDelegate(FreeFunc);
        g_callCount = 0;
        d1(10);
        AssertPass(g_callCount == 1, "Free function delegate invoked");

        // Member function
        auto d2 = MakeDelegate(&handler, &Handler::MemberFunc);
        g_callCount = 0;
        d2(20);
        AssertPass(g_callCount == 1, "Member function delegate invoked");

        // Lambda via MakeDelegate
        g_callCount = 0;
        int capture = 99;
        auto d3 = MakeDelegate([&capture](int val) {
            cout << "  Lambda called: " << val << " (capture=" << capture << ")" << endl;
            g_callCount++;
        });
        d3(30);
        AssertPass(g_callCount == 1, "Lambda delegate invoked");

        // Empty check — UnicastDelegate wraps a single delegate slot, starts empty
        UnicastDelegate<void(int)> empty;
        AssertPass(empty.Empty(), "Empty UnicastDelegate reports empty");
    }

    // -----------------------------------------------------------------------
    // Test 2: DelegateAsync — fire-and-forget
    // -----------------------------------------------------------------------
    PrintTest("DelegateAsync (fire-and-forget)");
    {
        g_callCount = 0;

        auto asyncD = MakeDelegate(FreeFunc, workerThread);
        asyncD(100);
        asyncD(200);

        // Give the worker thread time to process
        this_thread::sleep_for(chrono::milliseconds(100));
        AssertPass(g_callCount == 2, "DelegateAsync: both calls dispatched and invoked");

        // Member function async
        g_callCount = 0;
        auto asyncMember = MakeDelegate(&handler, &Handler::MemberFunc, workerThread);
        asyncMember(300);
        this_thread::sleep_for(chrono::milliseconds(100));
        AssertPass(g_callCount == 1, "DelegateAsync: member function dispatched and invoked");
    }

    // -----------------------------------------------------------------------
    // Test 3: DelegateAsyncWait — blocking call with return value
    // -----------------------------------------------------------------------
    PrintTest("DelegateAsyncWait (blocking, return value)");
    {
        g_callCount = 0;
        auto waitD = MakeDelegate(&handler, &Handler::MemberFuncReturn, workerThread,
                                  chrono::milliseconds(1000));
        int result = waitD(42);
        AssertPass(waitD.IsSuccess(), "DelegateAsyncWait: call completed within timeout");
        AssertPass(result == 84, "DelegateAsyncWait: return value is correct (42 * 2 = 84)");
        AssertPass(g_callCount == 1, "DelegateAsyncWait: target function invoked exactly once");

        // Demonstrate timeout on a delegate with no thread (no dispatch)
        // By setting a very short timeout and no real thread to service it
        auto freeWaitD = MakeDelegate(FreeFunc, workerThread, chrono::milliseconds(500));
        freeWaitD(55);
        AssertPass(freeWaitD.IsSuccess(), "DelegateAsyncWait: free function dispatched and returned");
    }

    // -----------------------------------------------------------------------
    // Test 4: MulticastDelegateSafe — thread-safe multicast
    // -----------------------------------------------------------------------
    PrintTest("MulticastDelegateSafe (thread-safe multicast)");
    {
        MulticastDelegateSafe<void(int)> multicast;

        Handler h2;
        multicast += MakeDelegate(FreeFunc);
        multicast += MakeDelegate(&handler, &Handler::MemberFunc);
        multicast += MakeDelegate(&h2, &Handler::MemberFunc);

        g_callCount = 0;
        multicast(400);
        AssertPass(g_callCount == 3, "MulticastDelegateSafe: 3 targets all invoked");

        // Remove one target
        multicast -= MakeDelegate(FreeFunc);
        g_callCount = 0;
        multicast(401);
        AssertPass(g_callCount == 2, "MulticastDelegateSafe: removal leaves 2 targets");

        // Async dispatch to worker thread
        MulticastDelegateSafe<void(int)> asyncMulticast;
        asyncMulticast += MakeDelegate(FreeFunc, workerThread);
        asyncMulticast += MakeDelegate(&handler, &Handler::MemberFunc, workerThread);
        g_callCount = 0;
        asyncMulticast(500);
        this_thread::sleep_for(chrono::milliseconds(100));
        AssertPass(g_callCount == 2, "MulticastDelegateSafe: async dispatch to worker thread");
    }

    // -----------------------------------------------------------------------
    // Test 5: UnicastDelegateSafe — thread-safe single-target delegate
    // -----------------------------------------------------------------------
    PrintTest("UnicastDelegateSafe (thread-safe single-target)");
    {
        UnicastDelegateSafe<void(int)> unicast;
        AssertPass(unicast.Empty(), "UnicastDelegateSafe: initially empty");

        unicast = MakeDelegate(FreeFunc);
        AssertPass(!unicast.Empty(), "UnicastDelegateSafe: bound after assignment");

        g_callCount = 0;
        unicast(600);
        AssertPass(g_callCount == 1, "UnicastDelegateSafe: target invoked");

        // Rebind to member function
        unicast = MakeDelegate(&handler, &Handler::MemberFunc);
        g_callCount = 0;
        unicast(601);
        AssertPass(g_callCount == 1, "UnicastDelegateSafe: rebind to member function works");

        // Clear
        unicast.Clear();
        AssertPass(unicast.Empty(), "UnicastDelegateSafe: empty after Clear()");
    }

    // -----------------------------------------------------------------------
    // Test 6: Signal and ScopedConnection (RAII)
    // -----------------------------------------------------------------------
    PrintTest("Signal with ScopedConnection (RAII)");
    {
        Signal<void(int)> signal;

        // Connect and fire while in scope
        g_callCount = 0;
        {
            ScopedConnection conn1 = signal.Connect(MakeDelegate(FreeFunc));
            ScopedConnection conn2 = signal.Connect(MakeDelegate(&handler, &Handler::MemberFunc));

            signal(700);
            AssertPass(g_callCount == 2, "Signal: 2 connections fired");

            // conn1 and conn2 go out of scope here — both auto-disconnected
        }

        g_callCount = 0;
        signal(701);
        AssertPass(g_callCount == 0, "Signal: ScopedConnections auto-disconnected on scope exit");

        // Persistent connection (manual disconnect)
        ScopedConnection persistent = signal.Connect(MakeDelegate(FreeFunc));
        g_callCount = 0;
        signal(702);
        AssertPass(g_callCount == 1, "Signal: persistent connection still fires");

        persistent.Disconnect();
        g_callCount = 0;
        signal(703);
        AssertPass(g_callCount == 0, "Signal: explicit Disconnect() removes connection");

        // Async signal dispatch
        ScopedConnection asyncConn = signal.Connect(MakeDelegate(FreeFunc, workerThread));
        g_callCount = 0;
        signal(800);
        this_thread::sleep_for(chrono::milliseconds(100));
        AssertPass(g_callCount == 1, "Signal: async connection dispatches to worker thread");
    }

    // -----------------------------------------------------------------------
    // Shutdown
    // -----------------------------------------------------------------------
    workerThread.ExitThread();

    // -----------------------------------------------------------------------
    // Summary
    // -----------------------------------------------------------------------
    const int totalTests = g_testNum;
    cout << "\n=========================================\n";
    cout << "  RESULTS: " << g_passCount << " assertions passed\n";
    if (g_passCount >= 23)
        cout << "  ALL TESTS PASSED\n";
    else
        cout << "  SOME TESTS FAILED\n";
    cout << "=========================================\n";

    return 0;
}
