/// @file main.c
/// FreeRTOS on a Cortex-M3 (QEMU mps2-an385): the kernel hooks
/// FreeRTOSConfig.h asks for, and newlib's malloc lock. The tests themselves
/// are main_delegate.cpp, the same as the Win32 simulator build's.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <reent.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bsp.h"

extern void main_delegate(void);

int main(void)
{
    printf("\n%s\n", BspName());
    main_delegate();    // starts the scheduler; doesn't return
    return 0;
}

// DelegateMQ allocates with new/malloc from several tasks; newlib's malloc
// is thread safe only if these lock. Overrides retarget.c's weak no-ops.
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

void vAssertCalled(unsigned long ulLine, const char *const pcFileName)
{
    taskDISABLE_INTERRUPTS();
    printf("\nconfigASSERT failed: %s line %lu\n", pcFileName, ulLine);
    BspExit(3);
}

void vApplicationMallocFailedHook(void)
{
    printf("\nFreeRTOS heap exhausted (configTOTAL_HEAP_SIZE)\n");
    BspExit(3);
}

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName)
{
    (void)pxTask;
    printf("\nStack overflow in task %s\n", pcTaskName);
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
