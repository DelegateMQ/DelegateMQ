/**
 * @file stm32f4xx_it.c
 * @brief Cortex-M4 exception handlers for the Pumptron F4 target.
 *
 * SVC_Handler and PendSV_Handler come from the FreeRTOS ARM_CM4F port (mapped
 * in FreeRTOSConfig.h). USART6_IRQHandler lives in main.cpp next to the
 * transport it serves.
 *
 * The fault exceptions store a core dump and reset (CoreDump.h).
 */

#include "stm32f4xx_hal.h"
#include "stm32f4_discovery.h"
#include "FreeRTOS.h"
#include "task.h"
#include "CoreDump.h"

extern void xPortSysTickHandler(void);

/* Naked, so no prologue touches the stack before the faulting context is
 * captured: bit 2 of EXC_RETURN (in LR) says whether the interrupted code was
 * on the process stack (a FreeRTOS task) or the main stack (an ISR, or main()
 * before the scheduler). Passes the stacked frame and EXC_RETURN on. */
#define CORE_DUMP_FAULT_HANDLER(name)          \
    __attribute__((naked)) void name(void)     \
    {                                          \
        __asm volatile(                        \
            "tst   lr, #4              \n"     \
            "ite   eq                  \n"     \
            "mrseq r0, msp             \n"     \
            "mrsne r0, psp             \n"     \
            "mov   r1, lr              \n"     \
            "b     CoreDump_FaultEntry \n");   \
    }

void NMI_Handler(void)        {}
CORE_DUMP_FAULT_HANDLER(HardFault_Handler)
CORE_DUMP_FAULT_HANDLER(MemManage_Handler)
CORE_DUMP_FAULT_HANDLER(BusFault_Handler)
CORE_DUMP_FAULT_HANDLER(UsageFault_Handler)
void DebugMon_Handler(void)   {}

void SysTick_Handler(void)
{
    /* HAL timebase (HAL_Delay, HAL timeouts) */
    HAL_IncTick();

    /* FreeRTOS tick, once the scheduler is running */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        xPortSysTickHandler();
}
