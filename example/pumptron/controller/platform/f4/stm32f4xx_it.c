/**
 * @file stm32f4xx_it.c
 * @brief Cortex-M4 exception handlers for the Pumptron F4 target.
 *
 * SVC_Handler and PendSV_Handler come from the FreeRTOS ARM_CM4F port (mapped
 * in FreeRTOSConfig.h). USART6_IRQHandler lives in main.cpp next to the
 * transport it serves.
 */

#include "stm32f4xx_hal.h"
#include "stm32f4_discovery.h"
#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);

static void FaultLoop(void)
{
    __disable_irq();
    BSP_LED_On(LED5);
    for (;;) {}
}

void NMI_Handler(void)        {}
void HardFault_Handler(void)  { FaultLoop(); }
void MemManage_Handler(void)  { FaultLoop(); }
void BusFault_Handler(void)   { FaultLoop(); }
void UsageFault_Handler(void) { FaultLoop(); }
void DebugMon_Handler(void)   {}

void SysTick_Handler(void)
{
    /* HAL timebase (HAL_Delay, HAL timeouts) */
    HAL_IncTick();

    /* FreeRTOS tick, once the scheduler is running */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        xPortSysTickHandler();
}
