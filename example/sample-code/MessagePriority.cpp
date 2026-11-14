/// @file
/// @brief Demonstrate dmq::Priority::URGENT messages bypassing queued NORMAL work.
///
/// Each async delegate carries a priority tag (LOW / NORMAL / HIGH). The thread
/// message queue is a std::priority_queue, so when a HIGH message is enqueued
/// while NORMAL messages are already waiting, the HIGH message moves to the front
/// and is processed next — before any of the waiting NORMAL messages.
///
/// Priority only affects queued messages. A message already being executed cannot
/// be interrupted; the HIGH message runs as soon as the current handler returns.

#include "MessagePriority.h"
#include "DelegateMQ.h"
#include <iostream>
#include <chrono>

using namespace dmq;
using namespace dmq::os;
using namespace std;

namespace Example
{
    // Simulate a time-consuming normal task so messages pile up in the queue
    static void NormalTask(int id)
    {
        cout << "[NORMAL] task " << id << " started\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        cout << "[NORMAL] task " << id << " done\n";
    }

    static void EmergencyTask()
    {
        cout << "[HIGH]   EMERGENCY handled - jumped ahead of queued NORMAL tasks\n";
    }

    // -------------------------------------------------------------------------
    // MessagePriorityExample
    //
    // Five NORMAL tasks are queued back-to-back (each takes 30ms). While task 1
    // is executing, an EMERGENCY (HIGH priority) is posted. Because the thread
    // uses a priority queue, EMERGENCY is placed ahead of tasks 2-5 that are
    // still waiting, and runs immediately after task 1 finishes.
    //
    // Expected output:
    //   [NORMAL] task 1 started       <- already dequeued when EMERGENCY arrives
    //   [NORMAL] task 1 done
    //   [HIGH]   EMERGENCY handled    <- dequeued before tasks 2-5
    //   [NORMAL] task N started       <- tasks 2-5 run in heap order (not insertion order)
    //   ...
    //
    // Note: tasks 2-5 all have equal NORMAL priority; std::priority_queue gives no
    // stability guarantee among equal-priority items, so their order is unspecified.
    // -------------------------------------------------------------------------
    void MessagePriorityExample()
    {
        Thread worker_thread("WorkerThread");
        worker_thread.CreateThread();

        // Queue five NORMAL tasks. Each takes 30ms, so tasks 2-5 wait in the queue.
        for (int i = 1; i <= 5; i++)
            MakeDelegate(&NormalTask, worker_thread)(i);

        // Task 1 is already executing (30ms). Sleep briefly then post the EMERGENCY.
        // At this point tasks 2-5 are all sitting in the priority queue.
        // EMERGENCY is inserted with HIGH priority and jumps to the front.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto d = MakeDelegate(&EmergencyTask, worker_thread);
        d.SetPriority(Priority::URGENT);
        d();

        // Wait for all work to finish: ~6 tasks * 30ms + margin
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        worker_thread.ExitThread();
    }
}
