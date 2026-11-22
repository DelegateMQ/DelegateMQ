/// @file IsrDelegateTests.cpp
/// @brief dmq::util::IsrDelegate on the desktop: posts from another thread
/// standing in for an ISR. The interrupt paths themselves are tested on real
/// interrupts by the QEMU samples (bare-metal/qemu, os/freertos-bare-metal,
/// os/freertos-esp32).

#include "DelegateMQ.h"
#include "UnitTestCommon.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace {

struct Point { int x; int y; };

std::vector<int> s_calls;           // sync target: called by ProcessAll() on this thread
void Record(int v) { s_calls.push_back(v); }

// Synchronous target: ProcessAll() calls it, in post order, with the arguments
void IsrDelegate_SyncTarget_CalledInOrderByProcessAll()
{
    s_calls.clear();
    IsrDelegate<void(int), 4> isr(MakeDelegate(&Record));
    DMQ_ASSERT_TRUE(isr.Post(1));
    DMQ_ASSERT_TRUE(isr.Post(2));
    DMQ_ASSERT_TRUE(isr.Post(3));
    DMQ_ASSERT_TRUE(s_calls.empty());               // nothing runs until ProcessAll()
    DMQ_ASSERT_TRUE(IsrDelegates::ProcessAll() == 3);
    DMQ_ASSERT_TRUE((s_calls == std::vector<int>{1, 2, 3}));
    DMQ_ASSERT_TRUE(IsrDelegates::ProcessAll() == 0);
    std::cout << "IsrDelegate_SyncTarget_CalledInOrderByProcessAll() complete!" << std::endl;
}

// A full ring drops the post, counts it, and keeps the earlier ones
void IsrDelegate_FullRing_DropsAndCounts()
{
    s_calls.clear();
    IsrDelegate<void(int), 2> isr(MakeDelegate(&Record));
    DMQ_ASSERT_TRUE(isr.Post(1));
    DMQ_ASSERT_TRUE(isr.Post(2));
    DMQ_ASSERT_TRUE(!isr.Post(3));
    DMQ_ASSERT_TRUE(isr.GetDroppedCount() == 1);
    IsrDelegates::ProcessAll();
    DMQ_ASSERT_TRUE((s_calls == std::vector<int>{1, 2}));
    DMQ_ASSERT_TRUE(isr.Post(4));                    // room again after ProcessAll()
    IsrDelegates::ProcessAll();
    DMQ_ASSERT_TRUE((s_calls == std::vector<int>{1, 2, 4}));
    std::cout << "IsrDelegate_FullRing_DropsAndCounts() complete!" << std::endl;
}

// Several arguments, a struct among them, are copied into the slot
void IsrDelegate_MultipleArgs_Copied()
{
    int sum = 0;
    IsrDelegate<void(Point, const int&, char)> isr(MakeDelegate(
        std::function<void(Point, const int&, char)>([&sum](Point p, const int& i, char c) { sum = p.x + p.y + i + c; })));
    int value = 100;
    DMQ_ASSERT_TRUE(isr.Post(Point{1, 2}, value, 3));
    value = 0;                                      // the post holds its own copy
    IsrDelegates::ProcessAll();
    DMQ_ASSERT_TRUE(sum == 106);
    std::cout << "IsrDelegate_MultipleArgs_Copied() complete!" << std::endl;
}

// Async target: ProcessAll() sends each post to the thread, which runs it
void IsrDelegate_AsyncTarget_RunsOnThread()
{
    Thread worker("IsrDelegateWorker");
    DMQ_ASSERT_TRUE(worker.CreateThread());
    std::atomic<int> count{0};
    std::atomic<int> wrongThread{0};
    IsrDelegate<void(int)> isr(MakeDelegate(std::function<void(int)>([&](int) {
        if (!worker.IsCurrentThread())
            wrongThread++;
        count++;
    }), worker));
    for (int i = 0; i < 5; i++)
        DMQ_ASSERT_TRUE(isr.Post(i));
    IsrDelegates::ProcessAll();
    for (int i = 0; i < 200 && count < 5; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    DMQ_ASSERT_TRUE(count == 5);
    DMQ_ASSERT_TRUE(wrongThread == 0);
    worker.ExitThread();
    std::cout << "IsrDelegate_AsyncTarget_RunsOnThread() complete!" << std::endl;
}

// The post handler runs on every successful post, and a poster on another
// thread racing ProcessAll() loses nothing: called + dropped == posted
std::atomic<int> s_postHandlerCalls{0};
void OnPost() { s_postHandlerCalls++; }

void IsrDelegate_ConcurrentPosts_NothingLost()
{
    std::atomic<int> called{0};
    std::atomic<int> outOfOrder{0};
    int last = 0;
    IsrDelegate<void(int), 8> isr(MakeDelegate(std::function<void(int)>([&](int seq) {
        if (seq <= last)
            outOfOrder++;
        last = seq;
        called++;
    })));
    s_postHandlerCalls = 0;
    IsrDelegates::SetPostHandler(&OnPost);

    const int POSTS = 20000;
    std::atomic<int> accepted{0};
    std::thread poster([&]() {
        for (int i = 1; i <= POSTS; i++)
            if (isr.Post(i))
                accepted++;
    });
    while (accepted + static_cast<int>(isr.GetDroppedCount()) < POSTS)
        IsrDelegates::ProcessAll();
    poster.join();
    IsrDelegates::ProcessAll();
    IsrDelegates::SetPostHandler(nullptr);

    DMQ_ASSERT_TRUE(called == accepted);
    DMQ_ASSERT_TRUE(called + static_cast<int>(isr.GetDroppedCount()) == POSTS);
    DMQ_ASSERT_TRUE(outOfOrder == 0);
    DMQ_ASSERT_TRUE(s_postHandlerCalls == accepted);
    std::cout << "IsrDelegate_ConcurrentPosts_NothingLost() complete! (dropped "
              << isr.GetDroppedCount() << "/" << POSTS << ")" << std::endl;
}

// A destroyed IsrDelegate leaves the list; ProcessAll() no longer calls it
void IsrDelegate_Destroyed_RemovedFromList()
{
    s_calls.clear();
    {
        IsrDelegate<void(int)> isr(MakeDelegate(&Record));
        DMQ_ASSERT_TRUE(isr.Post(7));
    }
    DMQ_ASSERT_TRUE(IsrDelegates::ProcessAll() == 0);
    DMQ_ASSERT_TRUE(s_calls.empty());
    std::cout << "IsrDelegate_Destroyed_RemovedFromList() complete!" << std::endl;
}

// A target may destroy or create another IsrDelegate during ProcessAll():
// the list walk skips the destroyed one and stays valid
std::unique_ptr<IsrDelegate<void(int)>> s_victim;
std::unique_ptr<IsrDelegate<void(int)>> s_created;
int s_victimCalls = 0;
void VictimTarget(int) { s_victimCalls++; }
void DestroyAndCreate(int)
{
    s_victim.reset();
    s_created = std::make_unique<IsrDelegate<void(int)>>(MakeDelegate(&VictimTarget));
}

void IsrDelegate_TargetDestroysAndCreatesOthers_ListStaysValid()
{
    s_victimCalls = 0;
    // Created first, so it's later in the list (new ones go to the head)
    s_victim = std::make_unique<IsrDelegate<void(int)>>(MakeDelegate(&VictimTarget));
    IsrDelegate<void(int)> killer(MakeDelegate(&DestroyAndCreate));
    DMQ_ASSERT_TRUE(s_victim->Post(1));
    DMQ_ASSERT_TRUE(killer.Post(1));
    IsrDelegates::ProcessAll();
    DMQ_ASSERT_TRUE(!s_victim);
    DMQ_ASSERT_TRUE(s_victimCalls == 0);             // destroyed before its turn: not called
    DMQ_ASSERT_TRUE(s_created->Post(2));             // the one created there works
    IsrDelegates::ProcessAll();
    DMQ_ASSERT_TRUE(s_victimCalls == 1);
    s_created.reset();
    std::cout << "IsrDelegate_TargetDestroysAndCreatesOthers_ListStaysValid() complete!" << std::endl;
}

} // namespace

void IsrDelegateTests()
{
    IsrDelegate_SyncTarget_CalledInOrderByProcessAll();
    IsrDelegate_FullRing_DropsAndCounts();
    IsrDelegate_MultipleArgs_Copied();
    IsrDelegate_AsyncTarget_RunsOnThread();
    IsrDelegate_ConcurrentPosts_NothingLost();
    IsrDelegate_Destroyed_RemovedFromList();
    IsrDelegate_TargetDestroysAndCreatesOthers_ListStaysValid();
    std::cout << "IsrDelegateTests() complete!" << std::endl;
}
