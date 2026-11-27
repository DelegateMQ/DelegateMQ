/// @file main.c
/// NUCLEO-H755ZI-Q, Cortex-M7: system setup, then the DelegateMQ FreeRTOS test
/// suite (os/freertos-bare-metal/main_delegate.cpp).
///
/// Boot (option bytes as shipped: both cores boot). The Cortex-M4 puts its
/// domain (D2) in STOP mode at reset and waits. This core sets the clocks,
/// releases it with a hardware semaphore (HSEM 0) notification, then starts
/// FreeRTOS. Clock, MPU and cache setup follow ST's BootCM4_CM7 template for
/// this board (STM32CubeH7 V1.12.1, Projects/NUCLEO-H745ZI-Q/Templates).

#include "board.h"
#include <stdio.h>

#define HSEM_ID_BOOT 0U

extern void main_delegate(void);

static void MPU_Config(void);
static void SystemClock_Config(void);

int main(void)
{
    // MemManage, BusFault and UsageFault get their own handlers (and names on
    // the console) instead of escalating to HardFault
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
    MPU_Config();
    SCB_EnableICache();
    SCB_EnableDCache();

    // Wait until the Cortex-M4 is in STOP mode (its domain clock off)
    int32_t timeout = 0xFFFF;
    while ((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET) && (timeout-- > 0)) {}
    if (timeout < 0)
        BoardFatal();

    HAL_Init();
    SystemClock_Config();
    BoardInit();    // LEDs and the console UART

    // Release the Cortex-M4: take and release HSEM 0, which it is waiting on
    __HAL_RCC_HSEM_CLK_ENABLE();
    HAL_HSEM_FastTake(HSEM_ID_BOOT);
    HAL_HSEM_Release(HSEM_ID_BOOT, 0);
    timeout = 0xFFFF;
    while ((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) == RESET) && (timeout-- > 0)) {}
    if (timeout < 0)
        printf("Cortex-M4 did not start (D2 clock not ready)\n");

    printf("\n%s\n", BspName());
    main_delegate();    // starts the scheduler; doesn't return
    return 0;
}

/// System clock 400 MHz from the PLL, fed by the 8 MHz bypass clock from the
/// ST-LINK. Cortex-M4 and buses (HCLK) 200 MHz, APB clocks 100 MHz.
static void SystemClock_Config(void)
{
    RCC_ClkInitTypeDef clk = {0};
    RCC_OscInitTypeDef osc = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_BYPASS;
    osc.HSIState = RCC_HSI_OFF;
    osc.CSIState = RCC_CSI_OFF;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 4;
    osc.PLL.PLLN = 400;
    osc.PLL.PLLFRACN = 0;
    osc.PLL.PLLP = 2;
    osc.PLL.PLLR = 2;
    osc.PLL.PLLQ = 4;
    osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    osc.PLL.PLLRGE = RCC_PLL1VCIRANGE_1;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        BoardFatal();

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_D1PCLK1 |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider = RCC_HCLK_DIV2;
    clk.APB3CLKDivider = RCC_APB3_DIV2;
    clk.APB1CLKDivider = RCC_APB1_DIV2;
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK)
        BoardFatal();

    // I/O compensation cell, recommended for fast I/O. It runs from the CSI.
    __HAL_RCC_CSI_ENABLE();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_CSIRDY) == RESET) {}
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_EnableCompensationCell();
}

/// Region 0: no access to undefined memory (stops speculative reads there;
/// ST's template). Region 1: SRAM4, shared with the Cortex-M4, non-cacheable:
/// the Cortex-M4 has no data cache, so a cached line here would hide one
/// core's writes from the other.
static void MPU_Config(void)
{
    MPU_Region_InitTypeDef r = {0};
    HAL_MPU_Disable();

    r.Enable = MPU_REGION_ENABLE;
    r.Number = MPU_REGION_NUMBER0;
    r.BaseAddress = 0x00;
    r.Size = MPU_REGION_SIZE_4GB;
    r.SubRegionDisable = 0x87;
    r.TypeExtField = MPU_TEX_LEVEL0;
    r.AccessPermission = MPU_REGION_NO_ACCESS;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.IsShareable = MPU_ACCESS_SHAREABLE;
    r.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);

    // TEX=1, C=0, B=0: Normal memory, non-cacheable. Not TEX=0 (C=0, B=0 is
    // Strongly-ordered there): unaligned accesses fault on Strongly-ordered
    // memory, and the shared-memory transport's memcpy makes them.
    r.Number = MPU_REGION_NUMBER1;
    r.BaseAddress = D3_SRAM_BASE;   // 0x38000000
    r.Size = MPU_REGION_SIZE_64KB;
    r.SubRegionDisable = 0x00;
    r.TypeExtField = MPU_TEX_LEVEL1;
    r.AccessPermission = MPU_REGION_FULL_ACCESS;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.IsShareable = MPU_ACCESS_SHAREABLE;
    r.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
