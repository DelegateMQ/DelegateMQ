/// @file stm32h7xx_it.c
/// NUCLEO-H755ZI-Q, Cortex-M7: SysTick and the fault handlers. SVC_Handler
/// and PendSV_Handler are FreeRTOS's (FreeRTOSConfig.h); TIM6's handler is
/// in bsp.c. Everything else keeps the startup code's default handler.

#include "board.h"
#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);

/// The HAL tick (HAL_GetTick(), used for HAL timeouts) and, once the
/// scheduler runs, the FreeRTOS tick: both 1 ms.
void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        xPortSysTickHandler();
}

static void FaultExit(const char* what)
{
    BoardWrite("\n", 1);
    BoardWrite(what, (int)__builtin_strlen(what));
    BspExit(4);
}

void NMI_Handler(void)        { FaultExit("NMI"); }
void HardFault_Handler(void)  { FaultExit("HardFault"); }
void MemManage_Handler(void)  { FaultExit("MemManage fault"); }
void BusFault_Handler(void)   { FaultExit("BusFault"); }
void UsageFault_Handler(void) { FaultExit("UsageFault"); }
