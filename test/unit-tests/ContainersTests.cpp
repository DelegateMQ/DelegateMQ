#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include <iostream>
#include <set>
#include <cstring>

using namespace dmq;
using namespace dmq::os;
using namespace std;
using namespace UnitTestData;

static int lambda1Int = 0;
static int lambda2Int = 0;
static int funcInt = 0;
static int classInt = 0;

void TFreeFunc(int i) { funcInt = i; }

class TClass
{
public:
    void Func(int i) { classInt = i; }
    void Func2(int i) { classInt = i + 1; }
};

static void UnicastDelegateTests()
{
    // Verify Constructor(DelegateType&&)
    {
        UnicastDelegate<void(int)> directInit(MakeDelegate(FreeFuncInt1));
        ASSERT_TRUE(directInit.Size() == 1);
        ASSERT_TRUE(!directInit.Empty());
        // Verify invocation works
        funcInt = 0; // Reset side effect var if used
        directInit(TEST_INT);
    }

    UnicastDelegateSafe<void(int)> src;
    src = MakeDelegate(FreeFuncInt1);
    ASSERT_TRUE(src.Size() == 1);
    auto dest = std::move(src);
    ASSERT_TRUE(src.Size() == 0);
    ASSERT_TRUE(dest.Size() == 1);

    UnicastDelegate<void(int)> src2(dest);
    ASSERT_TRUE(src2.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    dest.Clear();
    src = MakeDelegate(&FreeFuncInt1);
    ASSERT_TRUE(src.Size() == 1);
    dest = std::move(src);
    ASSERT_TRUE(src.Size() == 0);
    ASSERT_TRUE(dest.Size() == 1);

    src = dest;
    ASSERT_TRUE(src.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    dest.Clear();
    ASSERT_TRUE(dest.Size() == 0);

    src = MakeDelegate(&FreeFuncInt1);
    ASSERT_TRUE(src.Size() == 1);
    src = nullptr;
    ASSERT_TRUE(src.Size() == 0);

    dest.Clear();
    src = MakeDelegate(&FreeFuncInt1);
    dest = src;
    ASSERT_TRUE(src.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    // Invoke target
    src(TEST_INT);
    src.Broadcast(TEST_INT);

    src.Clear();
    src(TEST_INT);
    src.Broadcast(TEST_INT);
}

static void UnicastDelegateSafeTests()
{
    // Verify Constructor(DelegateType&&) for Safe version
    {
        UnicastDelegateSafe<void(int)> directInit(MakeDelegate(FreeFuncInt1));
        ASSERT_TRUE(directInit.Size() == 1);
        ASSERT_TRUE(!directInit.Empty());
        directInit(TEST_INT);
    }

    UnicastDelegateSafe<void(int)> src;
    src = MakeDelegate(FreeFuncInt1);
    ASSERT_TRUE(src.Size() == 1);
    auto dest = std::move(src);
    ASSERT_TRUE(src.Size() == 0);
    ASSERT_TRUE(dest.Size() == 1);

    UnicastDelegateSafe<void(int)> src2(dest);
    ASSERT_TRUE(src2.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    dest.Clear();
    src = MakeDelegate(&FreeFuncInt1);
    ASSERT_TRUE(src.Size() == 1);
    dest = std::move(src);
    ASSERT_TRUE(src.Size() == 0);
    ASSERT_TRUE(dest.Size() == 1);

    src = dest;
    ASSERT_TRUE(src.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    dest.Clear();
    ASSERT_TRUE(dest.Size() == 0);

    src = MakeDelegate(&FreeFuncInt1);
    ASSERT_TRUE(src.Size() == 1);
    src = nullptr;
    ASSERT_TRUE(src.Size() == 0);

    dest.Clear();
    src = MakeDelegate(&FreeFuncInt1);
    dest = src;
    ASSERT_TRUE(src.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    // Invoke target
    src(TEST_INT);
    src.Broadcast(TEST_INT);

    src.Clear();
    src(TEST_INT);
    src.Broadcast(TEST_INT);
}

static void MulticastDelegateTests()
{
    // Verify Constructor(DelegateType&&)
    {
        MulticastDelegate<void(int)> directInit(MakeDelegate(FreeFuncInt1));
        ASSERT_TRUE(directInit.Size() == 1);
        ASSERT_TRUE(!directInit.Empty());

        // Verify we can add more to it after construction
        directInit += MakeDelegate(FreeFuncInt1);
        ASSERT_TRUE(directInit.Size() == 2);
    }

    MulticastDelegate<void(int)> src;
    src += MakeDelegate(&FreeFuncInt1);
    ASSERT_TRUE(src.Size() == 1);
    auto dest = std::move(src);
    ASSERT_TRUE(src.Size() == 0);
    ASSERT_TRUE(dest.Size() == 1);

    MulticastDelegate<void(int)> src2(dest);
    ASSERT_TRUE(src2.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    dest.Clear();
    src += MakeDelegate(&FreeFuncInt1);
    dest = std::move(src);
    ASSERT_TRUE(src.Size() == 0);
    ASSERT_TRUE(dest.Size() == 1);

    src = dest;
    ASSERT_TRUE(src.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    dest.Clear();
    ASSERT_TRUE(dest.Size() == 0);

    src += MakeDelegate(&FreeFuncInt1);
    ASSERT_TRUE(src.Size() == 2);
    src = nullptr;
    ASSERT_TRUE(src.Size() == 0);

    dest.Clear();
    src.PushBack(MakeDelegate(&FreeFuncInt1));
    dest = src;
    ASSERT_TRUE(src.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    src += DelegateFree<void(int)>();
    src += DelegateFree<void(int)>();
    ASSERT_TRUE(src.Size() == 3);
    src.Remove(DelegateFree<void(int)>());
    ASSERT_TRUE(src.Size() == 2);

    // Invoke all targets
    src(TEST_INT);
    src.Broadcast(TEST_INT);

    TClass testClass;
    std::function lambda1 = [](int i) { lambda1Int = i; };
    std::function lambda2 = [](int i) { lambda2Int = i; };

    funcInt = 0, classInt = 0, lambda1Int = 0, lambda2Int = 0;
    src.Clear();
    src += MakeDelegate(&TFreeFunc);
    src += MakeDelegate(&testClass, &TClass::Func);
    src += MakeDelegate(lambda1);
    src += MakeDelegate(lambda2);
    src -= MakeDelegate(lambda1);
    src(TEST_INT);
    ASSERT_TRUE(funcInt == TEST_INT);
    ASSERT_TRUE(classInt == TEST_INT);
    ASSERT_TRUE(lambda1Int == 0);
    ASSERT_TRUE(lambda2Int == TEST_INT);
    ASSERT_TRUE(src.Size() == 3);

    funcInt = 0, classInt = 0, lambda1Int = 0, lambda2Int = 0;
    src.Clear();
    src += MakeDelegate(&TFreeFunc);
    src += MakeDelegate(&testClass, &TClass::Func);
    src += MakeDelegate(lambda1);
    src += MakeDelegate(lambda2);
    src -= MakeDelegate(lambda2);
    src(TEST_INT);
    ASSERT_TRUE(funcInt == TEST_INT);
    ASSERT_TRUE(classInt == TEST_INT);
    ASSERT_TRUE(lambda1Int == TEST_INT);
    ASSERT_TRUE(lambda2Int == 0);
    ASSERT_TRUE(src.Size() == 3);

    funcInt = 0, classInt = 0, lambda1Int = 0, lambda2Int = 0;
    src.Clear();
    src += MakeDelegate(&TFreeFunc);
    src += MakeDelegate(&testClass, &TClass::Func);
    src += MakeDelegate(lambda1);
    src += MakeDelegate(lambda2);
    src -= MakeDelegate(&testClass, &TClass::Func);
    src(TEST_INT);
    src -= MakeDelegate(&testClass, &TClass::Func2);  // Nothing to remove; not registered
    ASSERT_TRUE(src.Size() == 3);
    ASSERT_TRUE(funcInt == TEST_INT);
    ASSERT_TRUE(classInt == 0);
    ASSERT_TRUE(lambda1Int == TEST_INT);
    ASSERT_TRUE(lambda2Int == TEST_INT);
    ASSERT_TRUE(src.Size() == 3);

    funcInt = 0, classInt = 0, lambda1Int = 0, lambda2Int = 0;
    src.Clear();
    src += MakeDelegate(&TFreeFunc);
    src += MakeDelegate(&testClass, &TClass::Func);
    src += MakeDelegate(lambda1);
    src += MakeDelegate(lambda2);
    src -= MakeDelegate(&TFreeFunc);
    src(TEST_INT);
    ASSERT_TRUE(funcInt == 0);
    ASSERT_TRUE(classInt == TEST_INT);
    ASSERT_TRUE(lambda1Int == TEST_INT);
    ASSERT_TRUE(lambda2Int == TEST_INT);
    ASSERT_TRUE(src.Size() == 3);

    src -= MakeDelegate(&FreeFuncInt1);
    ASSERT_TRUE(src.Size() == 3);

    src.Clear();
    src(TEST_INT);
    src.Broadcast(TEST_INT);

#if 0
    // Example shows std::function target limitations. Not a normal usage case.
    // Use MakeDelegate() to create delegates works correctly with delegate 
    // containers.
    src.Clear();
    TClass t1, t2;
    std::function<void(int)> f1 = std::bind(&TClass::Func, &t1, std::placeholders::_1);
    std::function<void(int)> f2 = std::bind(&TClass::Func2, &t2, std::placeholders::_1);
    src += MakeDelegate(f1);
    src += MakeDelegate(f2);
    src -= MakeDelegate(f2);   // Should remove f2, not f1!
    src(TEST_INT);
    ASSERT_TRUE(classInt == TEST_INT);
#endif
}

static void MulticastDelegateSafeTests()
{
    // Verify Constructor(DelegateType&&) for Safe version
    {
        MulticastDelegateSafe<void(int)> directInit(MakeDelegate(FreeFuncInt1));
        ASSERT_TRUE(directInit.Size() == 1);
        ASSERT_TRUE(!directInit.Empty());
        directInit(TEST_INT);
    }

    MulticastDelegateSafe<void(int)> src;
    src += MakeDelegate(&FreeFuncInt1);
    ASSERT_TRUE(src.Size() == 1);
    auto dest = std::move(src);
    ASSERT_TRUE(src.Size() == 0);
    ASSERT_TRUE(dest.Size() == 1);

    MulticastDelegateSafe<void(int)> src2(dest);
    ASSERT_TRUE(src2.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    src.Clear();
    dest.Clear();
    src += MakeDelegate(&FreeFuncInt1);
    dest = std::move(src);
    ASSERT_TRUE(src.Size() == 0);
    ASSERT_TRUE(dest.Size() == 1);

    src = dest;
    ASSERT_TRUE(src.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    dest.Clear();
    ASSERT_TRUE(dest.Size() == 0);

    src += MakeDelegate(&FreeFuncInt1);
    ASSERT_TRUE(src.Size() == 2);
    src = nullptr;
    ASSERT_TRUE(src.Size() == 0);

    dest.Clear();
    src.PushBack(MakeDelegate(&FreeFuncInt1));
    dest = src;
    ASSERT_TRUE(src.Size() == 1);
    ASSERT_TRUE(dest.Size() == 1);

    src += DelegateFree<void(int)>();
    src += DelegateFree<void(int)>();
    ASSERT_TRUE(src.Size() == 3);
    src.Remove(DelegateFree<void(int)>());
    ASSERT_TRUE(src.Size() == 2);

    // Invoke all targets
    src(TEST_INT);
    src.Broadcast(TEST_INT);

    src.Clear();
    src(TEST_INT);
    src.Broadcast(TEST_INT);
}

// --- Reentrancy Verification Tests ---

// Global state for reentrant tests
static MulticastDelegate<void(int)>* g_ReentrantContainer = nullptr;
static int g_ReentrantRunCount = 0;

class ReentrantTester
{
public:
    void RemoveSelf(int val) {
        g_ReentrantRunCount++;
        // Remove THIS delegate while it is currently executing
        if (g_ReentrantContainer) {
            g_ReentrantContainer->Remove(MakeDelegate(this, &ReentrantTester::RemoveSelf));
        }
    }

    void RemoveOther(int val); // Defined below
};

static ReentrantTester g_Tester1;
static ReentrantTester g_Tester2;

void ReentrantTester::RemoveOther(int val) {
    g_ReentrantRunCount++;
    // Remove Tester2 while Tester1 is executing
    if (g_ReentrantContainer) {
        g_ReentrantContainer->Remove(MakeDelegate(&g_Tester2, &ReentrantTester::RemoveSelf));
    }
}

static void MulticastReentrantTests()
{
    // Test 1: Self-Removal (A delegate removes itself during invocation)
    {
        MulticastDelegate<void(int)> del;
        g_ReentrantContainer = &del;
        g_ReentrantRunCount = 0;

        del += MakeDelegate(&g_Tester1, &ReentrantTester::RemoveSelf);
        ASSERT_TRUE(del.Size() == 1);

        // Invoke: Should run, increment count, then safely remove itself without crashing
        del(TEST_INT);

        // Verification
        ASSERT_TRUE(g_ReentrantRunCount == 1); // It ran once
        ASSERT_TRUE(del.Size() == 0);          // It was successfully removed after execution
        ASSERT_TRUE(del.Empty());
    }

    // Test 2: Remove Next (Delegate A removes Delegate B before B runs)
    {
        MulticastDelegate<void(int)> del;
        g_ReentrantContainer = &del;
        g_ReentrantRunCount = 0;

        // Add Tester1 (The Remover)
        del += MakeDelegate(&g_Tester1, &ReentrantTester::RemoveOther);
        // Add Tester2 (The Victim)
        del += MakeDelegate(&g_Tester2, &ReentrantTester::RemoveSelf);

        ASSERT_TRUE(del.Size() == 2);

        // Invoke: 
        // 1. Tester1 runs -> Removes Tester2 from list (lazy deletion).
        // 2. Iterator moves to Tester2.
        // 3. Logic sees Tester2 is null (removed). Skips execution.
        // 4. Cleanup.
        del(TEST_INT);

        // Verification
        ASSERT_TRUE(g_ReentrantRunCount == 1); // Only Tester1 should have run
        ASSERT_TRUE(del.Size() == 1);          // Tester2 should be gone, Tester1 remains
    }

    // Test 3: Recursive Broadcast (A delegate triggers another broadcast)
    {
        MulticastDelegate<void(int)> del;
        int recursiveCount = 0;

        // A lambda that invokes the container again up to a limit
        std::function<void(int)> recursiveFunc = [&](int depth) {
            recursiveCount++;
            if (depth > 0) {
                // Recursive call with decremented depth
                del(depth - 1);
            }
            };

        del += MakeDelegate(recursiveFunc);

        // Invoke with depth 2: 
        // Call(2) -> Count=1 -> Call(1) -> Count=2 -> Call(0) -> Count=3
        del(2);

        ASSERT_TRUE(recursiveCount == 3);

        // Ensure RAII guard released the lock count correctly
        del.Remove(MakeDelegate(recursiveFunc));
        ASSERT_TRUE(del.Empty());
    }
}

static void MulticastDelegateSafeReentrantTests()
{
    // Test 0: Verify Equality Logic (Diagnostics)
    {
        // Create two delegates pointing to the SAME object and function
        auto d1 = MakeDelegate(&g_Tester1, &ReentrantTester::RemoveSelf);
        auto d2 = MakeDelegate(&g_Tester1, &ReentrantTester::RemoveSelf);

        // If this fails, Remove() will never find the delegate in the list
        ASSERT_TRUE(d1 == d2);
    }

    // Test 1: Self-Removal Safe
    {
        MulticastDelegateSafe<void(int)> del;
        g_ReentrantContainer = &del;
        g_ReentrantRunCount = 0;

        // Add the delegate
        del += MakeDelegate(&g_Tester1, &ReentrantTester::RemoveSelf);

        // Pre-check size
        ASSERT_TRUE(del.Size() == 1);

        // Execute
        // 1. operator() locks mutex
        // 2. Callback RemoveSelf runs
        // 3. RemoveSelf calls Remove(...) -> calls Base::Remove (no lock, but safe as same thread)
        // 4. Base::Remove sees count > 0 -> Resets pointer to null
        // 5. Loop finishes
        // 6. Cleanup() removes nulls
        del(TEST_INT);

        // Verification
        ASSERT_TRUE(g_ReentrantRunCount == 1);
        ASSERT_TRUE(del.Size() == 0);
    }
}

static void SignalTests()
{
    // Test 1: Manual Disconnect
    {
        Signal<void(int*)> sig;
        int callCount = 0;

        // Connect returns ScopedConnection — moved into 'conn'
        ScopedConnection conn = sig.Connect(MakeDelegate(+[](int* cnt) { (*cnt)++; }));

        ASSERT_TRUE(conn.IsConnected());

        sig(&callCount);
        ASSERT_TRUE(callCount == 1);

        // Disconnect
        conn.Disconnect();
        ASSERT_TRUE(!conn.IsConnected());

        // Fire again
        sig(&callCount);
        ASSERT_TRUE(callCount == 1);
    }

    // Test 2: RAII Scoped Disconnect
    {
        Signal<void(int*)> sig;
        int callCount = 0;
        {
            // Connect directly into ScopedConnection (Move)
            ScopedConnection scopedConn(sig.Connect(MakeDelegate(+[](int* cnt) { (*cnt)++; })));

            ASSERT_TRUE(scopedConn.IsConnected());
            sig(&callCount);
            ASSERT_TRUE(callCount == 1);

        } // Destructor runs

        ASSERT_TRUE(sig.Size() == 0);
        sig(&callCount);
        ASSERT_TRUE(callCount == 1);
    }

    // Test 3: Move Semantics (move-construction)
    {
        Signal<void(int*)> sig;
        ScopedConnection conn1 = sig.Connect(MakeDelegate(+[](int*) {}));

        ASSERT_TRUE(conn1.IsConnected());

        // Explicit move
        ScopedConnection conn2 = std::move(conn1);

        ASSERT_TRUE(conn2.IsConnected());
        ASSERT_TRUE(!conn1.IsConnected()); // conn1 is now empty
        ASSERT_TRUE(sig.Size() == 1);

        conn2.Disconnect();
        ASSERT_TRUE(sig.Size() == 0);
    }

    // Test 4: Move-assignment of ScopedConnection
    {
        Signal<void(int*)> sig;
        ScopedConnection conn1 = sig.Connect(MakeDelegate(+[](int*) {}));
        ScopedConnection conn2;

        ASSERT_TRUE(conn1.IsConnected());
        ASSERT_TRUE(!conn2.IsConnected());
        ASSERT_TRUE(sig.Size() == 1);

        conn2 = std::move(conn1);

        ASSERT_TRUE(!conn1.IsConnected());
        ASSERT_TRUE(conn2.IsConnected());
        ASSERT_TRUE(sig.Size() == 1);

        conn2.Disconnect();
        ASSERT_TRUE(sig.Size() == 0);
    }

    // Test 5: Multiple subscribers — all fire, individual disconnect works
    {
        Signal<void(int*)> sig;
        int cnt1 = 0, cnt2 = 0, cnt3 = 0;

        ScopedConnection c1 = sig.Connect(MakeDelegate(+[](int* p) { (*p)++; }));
        ScopedConnection c2 = sig.Connect(MakeDelegate(+[](int* p) { (*p)++; }));
        ScopedConnection c3 = sig.Connect(MakeDelegate(+[](int* p) { (*p)++; }));

        ASSERT_TRUE(sig.Size() == 3);
        ASSERT_TRUE(!sig.Empty());

        sig(&cnt1);
        ASSERT_TRUE(cnt1 == 3); // all three incremented the same counter

        // Disconnect middle subscriber; remaining two still fire
        c2.Disconnect();
        ASSERT_TRUE(sig.Size() == 2);

        cnt1 = 0;
        sig(&cnt1);
        ASSERT_TRUE(cnt1 == 2);
    }

    // Test 6: Empty() and Clear()
    {
        Signal<void()> sig;

        ASSERT_TRUE(sig.Empty());
        ASSERT_TRUE(sig.Size() == 0);

        ScopedConnection c1 = sig.Connect(MakeDelegate(+[]() {}));
        ScopedConnection c2 = sig.Connect(MakeDelegate(+[]() {}));

        ASSERT_TRUE(!sig.Empty());
        ASSERT_TRUE(sig.Size() == 2);

        sig.Clear();

        // Clear() empties the subscriber list but does NOT invalidate connection tokens.
        // ScopedConnections still hold a live watcher; IsConnected() is still true.
        ASSERT_TRUE(sig.Empty());
        ASSERT_TRUE(sig.Size() == 0);
        ASSERT_TRUE(c1.IsConnected());
        ASSERT_TRUE(c2.IsConnected());

        // Explicit disconnect after Clear() is a safe no-op (list is already empty).
        c1.Disconnect();
        c2.Disconnect();
        ASSERT_TRUE(!c1.IsConnected());
        ASSERT_TRUE(!c2.IsConnected());
    }

    // Test 7: Signal destroyed while ScopedConnection is still alive.
    // The Connection's disconnect lambda holds a shared_ptr<State>, so State is
    // kept alive until the ScopedConnection is itself destroyed or disconnected.
    // IsConnected() checks m_watcher.expired() — which stays false because the
    // lambda's shared_ptr keeps State alive. The alive flag (set in ~Signal) is
    // what prevents UAF; Disconnect() checks it and becomes a safe no-op.
    {
        ScopedConnection conn;
        {
            Signal<void()> sig;
            conn = sig.Connect(MakeDelegate(+[]() {}));
            ASSERT_TRUE(conn.IsConnected());
        } // Signal destroyed — sets state->alive = false, but State object stays
          // alive because conn's lambda still holds shared_ptr<State>.

        // IsConnected() is still true (m_watcher not expired).
        ASSERT_TRUE(conn.IsConnected());

        // Disconnect() is safe: the lambda runs, sees alive == false, does nothing.
        conn.Disconnect();
        ASSERT_TRUE(!conn.IsConnected());
    }

    // Test 8: Disconnect from within a slot (reentrancy)
    {
        Signal<void()> sig;
        int fireCount = 0;

        ScopedConnection* connPtr = nullptr;
        ScopedConnection conn;

        // Slot that disconnects itself on first invocation
        std::function<void()> selfDisconnect = [&]() {
            fireCount++;
            if (connPtr) connPtr->Disconnect();
        };

        conn = sig.Connect(MakeDelegate(selfDisconnect));
        connPtr = &conn;

        ASSERT_TRUE(sig.Size() == 1);

        sig(); // fires once, then disconnects itself
        ASSERT_TRUE(fireCount == 1);
        ASSERT_TRUE(sig.Size() == 0);

        sig(); // no subscribers remain
        ASSERT_TRUE(fireCount == 1);
    }
}

static void SignalThreadSafeTests()
{
    // Test: Thread Safe Signal (Signal<> is inherently thread-safe)
    {
        dmq::Signal<void(int*)> sig;
        int callCount = 0;

        {
            ScopedConnection conn = sig.Connect(MakeDelegate(+[](int* cnt) { (*cnt)++; }));
            ASSERT_TRUE(sig.Size() == 1);
            sig(&callCount);
            ASSERT_TRUE(callCount == 1);
        } // Disconnect

        ASSERT_TRUE(sig.Size() == 0);
    }
}

void ContainersTests()
{
    UnicastDelegateTests();
    UnicastDelegateSafeTests();
    MulticastDelegateTests();
    MulticastDelegateSafeTests();

    MulticastReentrantTests();
    MulticastDelegateSafeReentrantTests();

    SignalTests();
    SignalThreadSafeTests();
}