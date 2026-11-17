/// @file main.c
/// FreeRTOS on a Cortex-M3 (QEMU mps2-an385): the kernel hooks
/// FreeRTOSConfig.h asks for, and newlib's malloc lock. The tests themselves
/// are main_delegate.cpp, the same as the Win32 simulator build's.

#include <stdio.h>
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
