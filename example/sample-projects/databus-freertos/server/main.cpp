/// @file main.cpp
/// @brief FreeRTOS DataBus server entry point (Windows simulator).
///
/// Runs FreeRTOS on the Win32 simulator port. The server task starts the
/// Server active object and drives the publish loop. A FreeRTOS software
/// timer ticks the DelegateMQ Timer subsystem every 10 ms.
///
/// Build note: requires 32-bit Windows target.
///   cmake -A Win32 -B build .
///
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.

#include "DelegateMQ.h"
#include "Server.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Heap initialisation (heap_5 requires explicit region definition)
// ---------------------------------------------------------------------------

#define mainREGION_1_SIZE   8201
#define mainREGION_2_SIZE   23905
#define mainREGION_3_SIZE   16807

static uint8_t ucHeap1[mainREGION_1_SIZE];
static uint8_t ucHeap2[mainREGION_2_SIZE];
static uint8_t ucHeap3[mainREGION_3_SIZE];

static void prvInitialiseHeap(void)
{
    const HeapRegion_t xHeapRegions[] =
    {
        { ucHeap1, sizeof(ucHeap1) },
        { ucHeap2, sizeof(ucHeap2) },
        { ucHeap3, sizeof(ucHeap3) },
        { NULL,    0               }
    };
    vPortDefineHeapRegions(xHeapRegions);
}

// ---------------------------------------------------------------------------
// FreeRTOS application hooks (required by FreeRTOSConfig.h)
// ---------------------------------------------------------------------------

extern "C" void vApplicationIdleHook(void)
{
    // Intentionally empty — FreeRTOS idle task
}

extern "C" void vApplicationTickHook(void)
{
    // Intentionally empty
}

extern "C" void vApplicationMallocFailedHook(void)
{
    printf("[FreeRTOS] Heap allocation failed!\n");
    for (;;);
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName)
{
    printf("[FreeRTOS] Stack overflow in task: %s\n", pcTaskName);
    for (;;);
}

extern "C" void vApplicationDaemonTaskStartupHook(void)
{
    // Called once when the timer/daemon task starts — nothing needed here
}

extern "C" void vAssertCalled(unsigned long ulLine, const char* const pcFileName)
{
    printf("[FreeRTOS] Assert failed: %s line %lu\n", pcFileName, ulLine);
    for (;;);
}

extern "C" void vApplicationGetIdleTaskMemory(
    StaticTask_t**          ppxIdleTaskTCBBuffer,
    StackType_t**           ppxIdleTaskStackBuffer,
    configSTACK_DEPTH_TYPE* puxIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t  uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *puxIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

extern "C" void vApplicationGetTimerTaskMemory(
    StaticTask_t**          ppxTimerTaskTCBBuffer,
    StackType_t**           ppxTimerTaskStackBuffer,
    configSTACK_DEPTH_TYPE* puxTimerTaskStackSize)
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t  uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

    *ppxTimerTaskTCBBuffer   = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *puxTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}

// ---------------------------------------------------------------------------
// Server task
// ---------------------------------------------------------------------------

static void vServerTask(void* /*pvParams*/)
{
    // Disable stdout buffering so printf output appears immediately
    setvbuf(stdout, NULL, _IONBF, 0);

#ifdef _WIN32
    // Initialise Winsock within the FreeRTOS task context
    dmq::util::NetworkContext winsock;
#endif

    if (!Server::GetInstance().Start())
    {
        printf("[Server] Startup failed — task exiting.\n");
        vTaskDelete(NULL);
        return;
    }

    // Run blocks in a vTaskDelay loop until Stop() is called
    Server::GetInstance().Run();

    Server::GetInstance().Stop();
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

#if defined(_WIN32)
// Windows 11 ignores timeBeginPeriod() for a process whose windows are hidden
// behind others or minimized. The FreeRTOS Win32 port's tick thread then
// sleeps ~15.6 ms per 1 ms tick, so the simulated clock runs ~15x slow until
// the console comes back to the front. Opt out of that, and of EcoQoS.
static void KeepFullSpeedInBackground()
{
#if defined(PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION)
    PROCESS_POWER_THROTTLING_STATE state = {};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                        PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    state.StateMask = 0;    // 0 = never throttle
    // Looked up at run time: not declared below _WIN32_WINNT 0x0602, absent on Windows 7.
    using SetProcInfoFn = BOOL (WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
    auto setProcInfo = reinterpret_cast<SetProcInfoFn>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetProcessInformation"));
    if (setProcInfo)
        setProcInfo(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
#endif
}
#endif

int main(void)
{
#if defined(_WIN32)
    KeepFullSpeedInBackground();
#endif
    prvInitialiseHeap();

    // Tick the DelegateMQ Timer subsystem every 10 ms via a FreeRTOS software timer
    TimerHandle_t sysTimer = xTimerCreate(
        "SysTimer",
        pdMS_TO_TICKS(10),
        pdTRUE,   // auto-reload
        NULL,
        [](TimerHandle_t) { dmq::util::Timer::ProcessTimers(); });
    xTimerStart(sysTimer, 0);

    // Server task — stack sized generously for Winsock + C++ runtime
    xTaskCreate(vServerTask, "Server", 4096, NULL, 3, NULL);

    printf("--- Starting FreeRTOS Scheduler (DataBus Server) ---\n");
    vTaskStartScheduler();

    // Should never reach here
    for (;;);
}
