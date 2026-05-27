/**
 * @file safety/main.cpp
 * @brief Safety CPU — Independent Hardware Monitor & Interlock
 * 
 * The Safety node is an autonomous monitor running on FreeRTOS (Win32 Sim).
 * It ensures the instrument remains in a safe state by:
 * 1. Monitoring real-time centrifuge RPM for overspeed conditions.
 * 2. Verifying the presence of "heartbeats" from the Controller and GUI.
 * 3. Enforcing hardware interlocks independently of the main sequence logic.
 * 4. Publishing a global FAULT topic to halt the system upon any violation.
 */

#include "DelegateMQ.h"
#include "system/System.h"
#include "extras/util/NetworkConnect.h"
#include <cstdio>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <new>

// ---------------------------------------------------------------------------
// Heap: redirect C++ dynamic allocation through the FreeRTOS heap.
// pvPortMalloc/vPortFree use taskENTER_CRITICAL, which prevents FreeRTOS from
// preempting a task mid-allocation. This eliminates the Win32 CRT heap
// CRITICAL_SECTION contention that causes deadlocks between FreeRTOS tasks.
// ---------------------------------------------------------------------------
void* operator new  (size_t n)                         { void* p = pvPortMalloc(n); configASSERT(p); return p; }
void* operator new[](size_t n)                         { void* p = pvPortMalloc(n); configASSERT(p); return p; }
void* operator new  (size_t n, const std::nothrow_t&) noexcept { return pvPortMalloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return pvPortMalloc(n); }
void  operator delete  (void* p)               noexcept { vPortFree(p); }
void  operator delete[](void* p)               noexcept { vPortFree(p); }
void  operator delete  (void* p, size_t)       noexcept { vPortFree(p); }
void  operator delete[](void* p, size_t)       noexcept { vPortFree(p); }

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace cellutron;

// ---------------------------------------------------------------------------
// Heap initialisation
// ---------------------------------------------------------------------------
#define mainREGION_1_SIZE   (512 * 1024)
#define mainREGION_2_SIZE   (256 * 1024)
#define mainREGION_3_SIZE   (256 * 1024)

alignas(16) static uint8_t ucHeap1[mainREGION_1_SIZE];
alignas(16) static uint8_t ucHeap2[mainREGION_2_SIZE];
alignas(16) static uint8_t ucHeap3[mainREGION_3_SIZE];

static void prvInitialiseHeap(void) {
    const HeapRegion_t xHeapRegions[] = {
        { ucHeap1, sizeof(ucHeap1) },
        { ucHeap2, sizeof(ucHeap2) },
        { ucHeap3, sizeof(ucHeap3) },
        { NULL,    0               }
    };
    vPortDefineHeapRegions(xHeapRegions);
}

// ---------------------------------------------------------------------------
// FreeRTOS hooks
// ---------------------------------------------------------------------------
extern "C" void vApplicationIdleHook(void) { 
#if defined(_WIN32) || defined(_WIN64)
    Sleep(1); 
#elif defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    usleep(1000);
#endif
}
extern "C" void vApplicationTickHook(void) {}
extern "C" void vApplicationMallocFailedHook(void) { printf("FreeRTOS Safety: Malloc Failed!\n"); for (;;); }
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
    printf("FreeRTOS Safety: STACK OVERFLOW in task '%s'!\n", pcTaskName);
    for (;;);
}
extern "C" void vApplicationDaemonTaskStartupHook(void) {}
extern "C" void vAssertCalled(unsigned long ulLine, const char* const pcFileName) {
    printf("FreeRTOS Safety: ASSERT FAIL at %s:%lu\n", pcFileName, ulLine);
    for (;;);
}
extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t** p, StackType_t** s, configSTACK_DEPTH_TYPE* sz) {
    static StaticTask_t x; static StackType_t st[configMINIMAL_STACK_SIZE];
    *p = &x; *s = st; *sz = configMINIMAL_STACK_SIZE;
}
extern "C" void vApplicationGetTimerTaskMemory(StaticTask_t** p, StackType_t** s, configSTACK_DEPTH_TYPE* sz) {
    static StaticTask_t x; static StackType_t st[configTIMER_TASK_STACK_DEPTH];
    *p = &x; *s = st; *sz = configTIMER_TASK_STACK_DEPTH;
}

// ---------------------------------------------------------------------------
// Safety Monitor Task
// ---------------------------------------------------------------------------

static void vSafetyTask(void* /*pvParams*/) {
    cellutron::System::GetInstance().Initialize();

    // Catch unhandled topics (sent but no subscribers)
    static auto unhandledConn = dmq::databus::DataBus::SubscribeUnhandled([](const dmq::xstring& topic) {
        printf("SAFETY WARNING: Unhandled topic: %s\n", topic.c_str());
    });

    // Catch technical errors (e.g. serialization failures)
    static auto errorConn = dmq::databus::DataBus::SubscribeError([](const dmq::xstring& topic, dmq::DelegateError error) {
        printf("SAFETY ERROR: Technical failure on topic: %s, Error: %d\n", topic.c_str(), (int)error);
    });

    for (;;) {
        cellutron::System::GetInstance().Tick(100);
        Thread::Sleep(std::chrono::milliseconds(100));
    }
}

static void vWatchdogTask(void* /*pvParams*/) {
    for (;;) {
        Thread::WatchdogCheckAll();

        static int count = 0;
        if (++count >= 50) { // Every 5 seconds
            printf("FreeRTOS Safety Heap: %u bytes free\n", (unsigned int)xPortGetFreeHeapSize());
            count = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int main(void) {
    prvInitialiseHeap();
    static dmq::util::NetworkContext networkContext;
    printf("Cellutron Safety Processor starting (FreeRTOS Simulator)...\n");

    TimerHandle_t sysTimer = xTimerCreate("SysTimer", pdMS_TO_TICKS(10), pdTRUE, NULL, [](TimerHandle_t) { dmq::util::Timer::ProcessTimers(); });
    xTimerStart(sysTimer, 0);

    xTaskCreate(vSafetyTask, "Safety", 4096, NULL, 6, NULL);
    xTaskCreate(vWatchdogTask, "Watchdog", 4096, NULL, configMAX_PRIORITIES - 1, NULL);

    vTaskStartScheduler();
    for (;;);
}
