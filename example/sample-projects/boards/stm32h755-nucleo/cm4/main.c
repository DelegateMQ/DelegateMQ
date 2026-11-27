/// @file main.c
/// NUCLEO-H755ZI-Q, Cortex-M4: the boot handshake, then a heartbeat on LD2.
///
/// At reset this core puts its domain (D2) in STOP mode and waits for the
/// Cortex-M7 to set the clocks and release it with a hardware semaphore (HSEM 0)
/// notification (ST's BootCM4_CM7 scheme). Then it blinks LD2 (yellow) once a
/// second: proof on the board that both cores run. FreeRTOS and the shared
/// memory link with the Cortex-M7 come next.

#include "stm32h7xx_hal.h"

#define HSEM_ID_BOOT 0U

int main(void)
{
    // Wait for the Cortex-M7's release
    __HAL_RCC_HSEM_CLK_ENABLE();
    HAL_HSEM_ActivateNotification(__HAL_HSEM_SEMID_TO_MASK(HSEM_ID_BOOT));
    HAL_PWREx_ClearPendingEvent();
    HAL_PWREx_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFE, PWR_D2_DOMAIN);
    __HAL_HSEM_CLEAR_FLAG(__HAL_HSEM_SEMID_TO_MASK(HSEM_ID_BOOT));

    // The clocks are set now: SystemCoreClock is this core's 200 MHz
    HAL_Init();

    __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_1;     // LD2, yellow
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &g);

    for (;;) {
        HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_1);
        HAL_Delay(500);
    }
}
