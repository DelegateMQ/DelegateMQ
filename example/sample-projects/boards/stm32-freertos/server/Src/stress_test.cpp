/**
  ******************************************************************************
  * @file           : stress_test.cpp
  * @brief          : DelegateMQ FreeRTOS Stress & Integrity Test
  * @version        : 1.0.0
  *
  * @details
  * This file implements a high-throughput stress test to verify the reliability,
  * thread-safety, and memory stability of DelegateMQ running on FreeRTOS.
  *
  * ARCHITECTURE: "Software Backpressure"
  * -------------------------------------
  * FreeRTOS queues do not natively block the *sender* thread inside the
  * DelegateMQ library when full (messages are dropped if the queue is full).
  * To prevent data loss during high-speed bursts, this test uses a Priority
  * Gradient:
  * * 1. SUBSCRIBERS (Consumers): Run at HIGH priority (tskIDLE_PRIORITY + 3).
  * 2. PUBLISHERS  (Producers): Run at LOW priority  (tskIDLE_PRIORITY + 2).
  * * This ensures that as long as there is work in the queue, the Consumers
  * will preempt the Producers and drain the queue before new messages are generated.
  *
  * TEST LOGIC:
  * -----------
  * 1. Two Publisher Tasks emit bursts of Sync (direct call) and Async (queued)
  * signals targeting multiple Subscribers.
  * 2. Atomic counters track exactly how many signals were fired vs. received.
  * 3. The test runs in cycles:
  * [Run 5s] -> [Stop Producers] -> [Drain Queues 1s] -> [Verify Counts].
  * * CONFIGURATION (Native Cortex-M4 support):
  * -----------------------------------------
  * - Counters: uint32_t std::atomic (Native hardware support, no library calls).
  * - Output:   Standard 'printf' redirected to SWV ITM Console (Port 0).
  * - Heap:     Relies on 'XALLOCATOR' (Fixed Block) to prevent fragmentation.
  *
  ******************************************************************************
  */

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "DelegateMQ.h"
#include <vector>
#include <memory>
#include <atomic>
#include <stdio.h>
#include <string> // Required for std::to_string

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// ============================================================================
// CONFIGURATION
// ============================================================================
const int NUM_PUBLISHERS = 2;
const int NUM_SUBSCRIBERS = 2; 

const int BURST_SIZE = 5;       
const int QUEUE_SIZE = 20;      
const int TEST_CYCLE_MS = 5000;

// Task Priorities
#define PRIO_PUBLISHER  ( tskIDLE_PRIORITY + 2 )
#define PRIO_MONITOR    ( tskIDLE_PRIORITY + 1 )

// ============================================================================
// GLOBALS
// ============================================================================
// Using uint32_t for native atomic support on Cortex-M4
static std::atomic<uint32_t> g_signalsFired{ 0 };
static std::atomic<uint32_t> g_syncReceived{ 0 };
static std::atomic<uint32_t> g_asyncReceived{ 0 };

static std::atomic<uint32_t> g_expectedSync{ 0 };
static std::atomic<uint32_t> g_expectedAsync{ 0 };

static std::atomic<bool> g_publishing_active{ false };

// ============================================================================
// PAYLOAD
// ============================================================================
struct Payload
{
    XALLOCATOR
    int id;
    int publisherId;
    int val; 

    Payload(int i, int p) : id(i), publisherId(p), val(0) {}
};

// ============================================================================
// PUBLISHER
// ============================================================================
class StressPublisher
{
public:
    XALLOCATOR
    using StressSig = void(const Payload&);

    dmq::Signal<StressSig> OnMessage;
    TaskHandle_t m_taskHandle = nullptr;
    int m_id = 0;

    void Start(int id) {
        m_id = id;
        xTaskCreate(TaskEntry, "Pub", 512, this, PRIO_PUBLISHER, &m_taskHandle);
    }

    static void TaskEntry(void* pvParams) {
        StressPublisher* self = (StressPublisher*)pvParams;
        int msgId = 0;
        
        const int syncSubs = NUM_SUBSCRIBERS / 2;
        const int asyncSubs = NUM_SUBSCRIBERS / 2;

        while(1) {
            if (g_publishing_active) {
                for (int i = 0; i < BURST_SIZE; ++i) {
                    Payload p(msgId++, self->m_id);

                    // Update expectations ATOMICALLY before firing
                    g_expectedSync += syncSubs;
                    g_expectedAsync += asyncSubs;
                    g_signalsFired++;

                    self->OnMessage(p); 
                }
            }
            // Throttle to prevent starvation
            vTaskDelay(pdMS_TO_TICKS(10)); 
        }
    }
};

// ============================================================================
// SUBSCRIBER
// ============================================================================
class StressSubscriber
{
public:
    XALLOCATOR

    Thread m_thread; 
    int m_id;

    StressSubscriber(int id) 
        // Pass only the name string to the Thread wrapper
        : m_thread("Sub" + std::to_string(id), QUEUE_SIZE), m_id(id)
    {
        m_thread.CreateThread();
    }

    void OnSyncEvent(const Payload& p) {
        g_syncReceived++;
    }

    void OnAsyncEvent(const Payload& p) {
        g_asyncReceived++;
    }
};

// ============================================================================
// MAIN STRESS TEST TASK
// ============================================================================
void StressTestTask(void* pvParameters) 
{
    printf("--- FreeRTOS Stress Test Initializing ---\n");
    printf("Pubs: %d | Subs: %d | Burst: %d\n", NUM_PUBLISHERS, NUM_SUBSCRIBERS, BURST_SIZE);

    std::vector<std::shared_ptr<StressPublisher>> publishers;
    for (int i = 0; i < NUM_PUBLISHERS; ++i) {
        publishers.push_back(std::make_shared<StressPublisher>());
        publishers.back()->Start(i);
    }

    std::vector<std::shared_ptr<StressSubscriber>> subscribers;
    std::vector<ScopedConnection> connections;

    for (int i = 0; i < NUM_SUBSCRIBERS; ++i) {
        auto sub = std::make_shared<StressSubscriber>(i);
        subscribers.push_back(sub);

        for (auto& pub : publishers) {
            if (i % 2 == 0) {
                connections.push_back(pub->OnMessage.Connect(
                    MakeDelegate(sub.get(), &StressSubscriber::OnSyncEvent)));
            } else {
                connections.push_back(pub->OnMessage.Connect(
                    MakeDelegate(sub.get(), &StressSubscriber::OnAsyncEvent, sub->m_thread)));
            }
        }
    }

    printf("Initialization Complete. Starting Loop.\n");

    int cycle = 1;
    while(1) 
    {
        printf("\n[Cycle %d] GO! Publishing for %dms...\n", cycle, TEST_CYCLE_MS);
        g_publishing_active = true;
        vTaskDelay(pdMS_TO_TICKS(TEST_CYCLE_MS));

        printf("[Cycle %d] STOP. Draining queues...\n", cycle);
        g_publishing_active = false;
        
        // Wait for Async queues to empty
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t fired = g_signalsFired.load();
        uint32_t expSync = g_expectedSync.load();
        uint32_t actSync = g_syncReceived.load();
        uint32_t expAsync = g_expectedAsync.load();
        uint32_t actAsync = g_asyncReceived.load();

        printf("----------------------------------\n");
        printf("Report:\n");
        printf("  Fired: %lu\n", (unsigned long)fired);
        printf("  Sync : %lu / %lu\n", (unsigned long)actSync, (unsigned long)expSync);
        printf("  Async: %lu / %lu\n", (unsigned long)actAsync, (unsigned long)expAsync);

        if (actSync == expSync && actAsync == expAsync) {
            BSP_LED_On(LED6); // Blue = Pass
            BSP_LED_Off(LED5);
            printf("[RESULT] PASS\n");
        } else {
            BSP_LED_On(LED5); // Red = Fail
            BSP_LED_Off(LED6);
            printf("[RESULT] FAIL - DATA LOSS DETECTED\n");
        }
        printf("----------------------------------\n");

        cycle++;
    }
}

void StartStressTest() {
    xTaskCreate(StressTestTask, "StressMain", 2048, NULL, PRIO_MONITOR, NULL);
}
