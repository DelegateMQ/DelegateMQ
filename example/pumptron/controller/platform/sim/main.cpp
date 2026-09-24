/**
 * @file controller/platform/sim/main.cpp
 * @brief Pumptron controller -- FreeRTOS simulator target (Windows/Linux).
 *
 * Runs the exact same PumpController as the STM32F4 target, on the FreeRTOS
 * Win32/POSIX simulator port, with two substitutions made here and nowhere
 * else:
 *   - SimBoard instead of F4Board (no real sensors/LEDs)
 *   - NetworkNode<UDP> instead of SerialLink<Stm32UartTransport>
 *
 * Start the GUI with `--udp` to talk to this process.
 */

#include "DelegateMQ.h"
#include "extras/util/NetworkConnect.h"
#include "extras/databus/NetworkNode.h"
#include "pump/PumpController.h"
#include "pump/LinkErrorReporter.h"
#include "util/Topology.h"
#include "SimBoard.h"
#include <cstdio>
#include <new>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#if defined(_WIN32)
    #include "port/transport/win32-udp/Win32UdpTransport.h"
    using UdpLink = dmq::databus::NetworkNode<dmq::transport::Win32UdpTransport>;
#else
    #include "port/transport/linux-udp/LinuxUdpTransport.h"
    using UdpLink = dmq::databus::NetworkNode<dmq::transport::LinuxUdpTransport>;
#endif

// ---------------------------------------------------------------------------
// Heap: route C++ allocation through the FreeRTOS heap so tasks are never
// preempted while holding the host CRT heap lock (see Cellutron's controller).
// ---------------------------------------------------------------------------
void* operator new  (size_t n)                                 { void* p = pvPortMalloc(n); configASSERT(p); return p; }
void* operator new[](size_t n)                                 { void* p = pvPortMalloc(n); configASSERT(p); return p; }
void* operator new  (size_t n, const std::nothrow_t&) noexcept { return pvPortMalloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return pvPortMalloc(n); }
void  operator delete  (void* p)                       noexcept { vPortFree(p); }
void  operator delete[](void* p)                       noexcept { vPortFree(p); }
void  operator delete  (void* p, size_t)               noexcept { vPortFree(p); }
void  operator delete[](void* p, size_t)               noexcept { vPortFree(p); }

using namespace pumptron;

alignas(16) static uint8_t ucHeap[512 * 1024];

static void InitialiseHeap()
{
    const HeapRegion_t regions[] = {
        { ucHeap, sizeof(ucHeap) },
        { nullptr, 0 }
    };
    vPortDefineHeapRegions(regions);
}

// ---------------------------------------------------------------------------
// FreeRTOS hooks
// ---------------------------------------------------------------------------
extern "C" void vApplicationIdleHook(void) {
#if defined(_WIN32)
    Sleep(1);
#else
    usleep(1000);
#endif
}
extern "C" void vApplicationTickHook(void) {}
extern "C" void vApplicationDaemonTaskStartupHook(void) {}
extern "C" void vApplicationMallocFailedHook(void) { printf("FreeRTOS: malloc failed!\n"); DMQ_ASSERT(); }
extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char* pcTaskName) {
    printf("FreeRTOS: stack overflow in task '%s'\n", pcTaskName);
    DMQ_ASSERT();
}
extern "C" void vAssertCalled(unsigned long ulLine, const char* const pcFileName) {
    printf("FreeRTOS: assert at %s:%lu\n", pcFileName, ulLine);
    DMQ_ASSERT();
}
extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t** tcb, StackType_t** stack, configSTACK_DEPTH_TYPE* size) {
    static StaticTask_t t; static StackType_t s[configMINIMAL_STACK_SIZE];
    *tcb = &t; *stack = s; *size = configMINIMAL_STACK_SIZE;
}
extern "C" void vApplicationGetTimerTaskMemory(StaticTask_t** tcb, StackType_t** stack, configSTACK_DEPTH_TYPE* size) {
    static StaticTask_t t; static StackType_t s[configTIMER_TASK_STACK_DEPTH];
    *tcb = &t; *stack = s; *size = configTIMER_TASK_STACK_DEPTH;
}

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------
static board::SimBoard s_board;
static pump::PumpController* s_pump = nullptr;
static UdpLink* s_link = nullptr;

static void MainTask(void*)
{
    // Objects that own dmq::os::Thread must be constructed after the scheduler starts.
    static pump::PumpController pumpController(s_board);
    static UdpLink link;
    s_pump = &pumpController;
    s_link = &link;

    // DataBus + link errors -> LINK_DEGRADED alarm / status resync.
    static pump::LinkErrorReporter errorReporter(link, pumpController);

    ConfigureControllerLink(link);
    link.Start("Controller", CONTROLLER_UDP_PORT);
    link.AddPeer("GUI", "127.0.0.1", GUI_UDP_PORT);

    pumpController.GetThread().SetThreadPriority(PRIORITY_PUMP);
    pumpController.Start(WATCHDOG_TIMEOUT);

    printf("Pumptron controller (FreeRTOS simulator) listening on UDP %u, GUI at %u\n",
           CONTROLLER_UDP_PORT, GUI_UDP_PORT);

    for (;;) {
        dmq::os::Thread::WatchdogCheckAll();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main()
{
    ::InstallCrashHandlers();
    InitialiseHeap();
    Logger_Init();
    static dmq::util::NetworkContext networkContext;
    printf("Pumptron controller starting (FreeRTOS simulator)...\n");

    TimerHandle_t sysTimer = xTimerCreate("SysTimer",
        pdMS_TO_TICKS(std::chrono::duration_cast<std::chrono::milliseconds>(TIMER_TICK_PERIOD).count()),
        pdTRUE, nullptr, [](TimerHandle_t) { dmq::util::Timer::ProcessTimers(); });
    xTimerStart(sysTimer, 0);

    xTaskCreate(MainTask, "Main", 4096, nullptr, configMAX_PRIORITIES - 1, nullptr);

    vTaskStartScheduler();
    for (;;);
}

