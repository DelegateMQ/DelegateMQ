/// @file hooks.c
/// NUCLEO-H755ZI-Q, Cortex-M7: the FreeRTOS hooks FreeRTOSConfig.h asks for,
/// newlib's malloc lock and the heap call counter for the suite's Test 14.
/// As in the suite's Cortex-M3 target (os/freertos-bare-metal/targets/arm-m3/
/// main.c), except that the failure hooks don't use printf.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <reent.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bsp.h"

// DelegateMQ allocates with new/malloc from several tasks; newlib's malloc
// is thread safe only if these lock. Overrides newlib's no-op lock.
// Never called from an interrupt: nothing here allocates in ISR context.
void __malloc_lock(struct _reent *r)
{
    (void)r;
    vTaskSuspendAll();
}

void __malloc_unlock(struct _reent *r)
{
    (void)r;
    (void)xTaskResumeAll();
}

// Count every heap call (linked with -Wl,--wrap=malloc,calloc,realloc; see
// target.cmake). C++ new calls malloc, so it is counted too. Test 14 checks the
// count doesn't change while the system runs.
static volatile uint32_t s_heapCalls = 0;
static void* volatile s_heapLastCaller = 0;    // return address of the last call
static volatile size_t s_heapLastSize = 0;

void* __real_malloc(size_t size);
void* __real_calloc(size_t n, size_t size);
void* __real_realloc(void* ptr, size_t size);

#define HEAP_CALL(n) (s_heapCalls++, s_heapLastSize = (n), s_heapLastCaller = __builtin_return_address(0))

void* __wrap_malloc(size_t size)              { HEAP_CALL(size); return __real_malloc(size); }
void* __wrap_calloc(size_t n, size_t size)    { HEAP_CALL(n * size); return __real_calloc(n, size); }
void* __wrap_realloc(void* ptr, size_t size)  { HEAP_CALL(size); return __real_realloc(ptr, size); }

uint32_t BspHeapCallCount(void) { return s_heapCalls; }

void* BspHeapLastCaller(size_t* size) { *size = s_heapLastSize; return s_heapLastCaller; }

// The failure hooks write the console directly, not through printf: printf
// can take newlib's locks or allocate, and after a stack overflow the less
// stack used the better. Interrupts are masked first, so BoardWrite() doesn't
// block on the console mutex.
int BoardWrite(const char* buf, int len);

static void HookWrite(const char* text)
{
    BoardWrite(text, (int)strlen(text));
}

void vAssertCalled(unsigned long ulLine, const char *const pcFileName)
{
    taskDISABLE_INTERRUPTS();
    char line[16];
    snprintf(line, sizeof line, "%lu", ulLine);
    HookWrite("\nconfigASSERT failed: ");
    HookWrite(pcFileName);
    HookWrite(" line ");
    HookWrite(line);
    HookWrite("\n");
    BspExit(3);
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    HookWrite("\nFreeRTOS heap exhausted (configTOTAL_HEAP_SIZE)\n");
    BspExit(3);
}

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName)
{
    (void)pxTask;
    taskDISABLE_INTERRUPTS();
    HookWrite("\nStack overflow in task ");
    HookWrite(pcTaskName);
    HookWrite("\n");
    BspExit(3);
}

void vApplicationIdleHook(void) {}
void vApplicationTickHook(void) {}
void vApplicationDaemonTaskStartupHook(void) {}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
