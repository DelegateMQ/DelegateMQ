/**
  * @file stress_tests.cpp
  * @brief High-throughput stress test.
  * @details 
  * - Uses 'Signal' (No Mutex) to prevent lock contention.
  * - Renamed member variable to fix shadowing error.
  */
#include "DelegateMQ.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <memory>
#include <atomic>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

// ============================================================================
// CONFIGURATION
// ============================================================================
const int TEST_DURATION_MS = 15000; 
const int BURST_SIZE = 10;          
const int LOOP_DELAY_TICKS = 1;     

// ============================================================================
// GLOBAL STATE
// ============================================================================
static std::atomic<uint32_t> g_signalsFired{ 0 };
static std::atomic<uint32_t> g_syncReceived{ 0 };
static std::atomic<uint32_t> g_asyncReceived{ 0 };
static std::atomic<bool> g_running{ true };

struct Payload {
    int id;
    int val;
    Payload(int i, int v) : id(i), val(v) {}
};

// ============================================================================
// PUBLISHER
// ============================================================================
class SimplePublisher
{
public:
    dmq::Signal<void(const Payload&)> m_signal;
    
    Thread m_thread;

    SimplePublisher() : m_thread("Pub_Thread") 
    {
        m_thread.SetThreadPriority(2); 
        m_thread.SetStackMem(nullptr, 2048); 
        m_thread.CreateThread();
    }

    ~SimplePublisher() { m_thread.ExitThread(); }

    void Start() {
        MakeDelegate(this, &SimplePublisher::WorkerLoop, m_thread).AsyncInvoke();
    }

    void WorkerLoop() {
        int msgId = 0;
        printf("[Pub] Loop Start.\n");
        
        while (g_running) {
            for (int i = 0; i < BURST_SIZE; ++i) {
                if (!g_running) break; 

                Payload p(msgId++, 123);
                g_signalsFired++;
                
                m_signal(p);
            }
            vTaskDelay(LOOP_DELAY_TICKS);
        }
        printf("[Pub] Loop Exit.\n");
    }

    void Stop() { m_thread.ExitThread(); }
};

// ============================================================================
// SUBSCRIBER
// ============================================================================
class SimpleSubscriber
{
public:
    Thread m_thread;

    SimpleSubscriber() : m_thread("Sub_Thread") 
    {
        m_thread.SetThreadPriority(3);
        m_thread.SetStackMem(nullptr, 2048); 
        m_thread.CreateThread();
    }

    ~SimpleSubscriber() { m_thread.ExitThread(); }

    void OnSync(const Payload& p) {
        g_syncReceived++;
    }

    void OnAsync(const Payload& p) {
        g_asyncReceived++;
    }
};

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================
void StartStressTests() {
    printf("\n=== STARTING OPTIMIZED STRESS TEST ===\n");
    
    g_running = true;
    g_signalsFired = 0;
    g_syncReceived = 0;
    g_asyncReceived = 0;

    {
        auto pub = std::make_shared<SimplePublisher>();
        auto sub = std::make_shared<SimpleSubscriber>();

        // Sync Connection
        ScopedConnection conn1 = pub->m_signal.Connect(
            MakeDelegate(sub.get(), &SimpleSubscriber::OnSync));
        
        // Async Connection
        ScopedConnection conn2 = pub->m_signal.Connect(
            MakeDelegate(sub.get(), &SimpleSubscriber::OnAsync, sub->m_thread));

        printf("Threads Active. Stabilizing (200ms)...\n");
        vTaskDelay(pdMS_TO_TICKS(200)); 

        printf("GO!\n");
        pub->Start();

        TickType_t startTick = xTaskGetTickCount();
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            uint32_t elapsed = (xTaskGetTickCount() - startTick) * portTICK_PERIOD_MS;
            
            printf("[%lu ms] Fired: %lu | Sync: %lu | Async: %lu\n", 
                (unsigned long)elapsed, 
                (unsigned long)g_signalsFired, 
                (unsigned long)g_syncReceived, 
                (unsigned long)g_asyncReceived);

            if (elapsed >= TEST_DURATION_MS) break;
        }

        printf("Flagging Stop...\n");
        g_running = false;
        vTaskDelay(pdMS_TO_TICKS(500)); 

        printf("Destroying Objects...\n");
    } 

    printf("=== TEST COMPLETE ===\n");
    printf("Final Fired: %lu\n", (unsigned long)g_signalsFired);
    printf("Final Sync:  %lu\n", (unsigned long)g_syncReceived);
    printf("Final Async: %lu\n", (unsigned long)g_asyncReceived);

    if (g_syncReceived > 0 && g_asyncReceived > 0) {
        printf("[PASS]\n");
        BSP_LED_On(LED4); 
    } else {
        printf("[FAIL]\n");
        BSP_LED_On(LED5); 
    }
}