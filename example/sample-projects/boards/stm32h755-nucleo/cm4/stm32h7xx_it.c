/// @file stm32h7xx_it.c
/// NUCLEO-H755ZI-Q, Cortex-M4: the HAL tick. Everything else keeps the
/// startup code's default handler.

#include "stm32h7xx_hal.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
}
