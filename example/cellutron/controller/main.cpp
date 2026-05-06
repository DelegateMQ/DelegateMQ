/**
 * @file controller/main.cpp
 * @brief Controller CPU — Process Sequencing & Hardware Control
 * 
 * The Controller is the "brain" of the instrument. It runs on FreeRTOS 
 * (Win32 Sim) and is responsible for:
 * 1. Orchestrating the high-level cell processing sequence (CellProcess SM).
 * 2. Executing atomic hardware actions (PumpProcess SM).
 * 3. Providing real-time setpoints to the centrifuge and actuators.
 * 4. Publishing system status and hardware feedback to the DataBus.
 */

#include "DelegateMQ.h"
#include "system/System.h"
#include "extras/util/NetworkConnect.h"
#include <cstdio>

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

static uint8_t ucHeap1[mainREGION_1_SIZE];
static uint8_t ucHeap2[mainREGION_2_SIZE];
static uint8_t ucHeap3[mainREGION_3_SIZE];

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
extern "C" void vApplicationIdleHook(void) { Sleep(1); }
extern "C" void vApplicationTickHook(void) {}
extern "C" void vApplicationMallocFailedHook(void) { printf("FreeRTOS: Malloc Failed!\n"); for (;;); }
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
    printf("FreeRTOS: STACK OVERFLOW in task '%s'!\n", pcTaskName);
    for (;;);
}
extern "C" void vApplicationDaemonTaskStartupHook(void) {}
extern "C" void vAssertCalled(unsigned long ulLine, const char* const pcFileName) {
    printf("FreeRTOS: ASSERT FAIL at %s:%lu\n", pcFileName, ulLine);
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
// Tasks
// ---------------------------------------------------------------------------

static void vControllerTask(void* /*pvParams*/) {
    cellutron::System::GetInstance().Initialize();

    for (;;) {
        cellutron::System::GetInstance().Tick(100);
        Thread::Sleep(std::chrono::milliseconds(100));
    }
}

static void vWatchdogTask(void* /*pvParams*/) {
    for (;;) {
        Thread::WatchdogCheckAll();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int main(void) {
    prvInitialiseHeap();
    static dmq::util::NetworkContext networkContext;
    printf("Cellutron Controller Processor starting (FreeRTOS Win32)...\n");

    TimerHandle_t sysTimer = xTimerCreate("SysTimer", pdMS_TO_TICKS(10), pdTRUE, NULL, [](TimerHandle_t) { dmq::util::Timer::ProcessTimers(); });
    xTimerStart(sysTimer, 0);

    xTaskCreate(vControllerTask, "Controller", 4096, NULL, 6, NULL);
    xTaskCreate(vWatchdogTask, "Watchdog", 4096, NULL, configMAX_PRIORITIES - 1, NULL);

    vTaskStartScheduler();
    for (;;);
}
