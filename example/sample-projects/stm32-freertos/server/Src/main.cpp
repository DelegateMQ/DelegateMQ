/**
  ******************************************************************************
  * @file           : main.cpp
  * @brief          : DelegateMQ + FreeRTOS Integration Demo on STM32F4-Discovery
  * @version        : 1.2.0
  ******************************************************************************
  */
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "DelegateMQ.h"
#include <stdio.h>

// Include the Transport Header to define the Global Instance + Callback
#include "../../src/delegate-mq/port/transport/stm32-uart/Stm32UartTransport.h"

// ============================================================================
// TEST SELECTION
// ============================================================================
#define RUN_SIMPLE_TESTS
//#define RUN_STRESS_TESTS
//#define RUN_NETWORK_TESTS
//#define RUN_DATABUS_TESTS

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
extern void StartSimpleTests();
extern void StartStressTests();
extern void StartNetworkTests();
extern void StartDatabusTests();

// Global Handles
UART_HandleTypeDef huart6;
dmq::rpc::RemoteDispatcher* g_netEngine = nullptr;

// --- DEFINE GLOBAL TRANSPORT INSTANCE ---
dmq::transport::Stm32UartTransport* g_uartTransportInstance = nullptr;

// --- DEFINE HAL CALLBACK ---
extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (g_uartTransportInstance && huart->Instance == USART6) {
        g_uartTransportInstance->OnRxCplt();
    }
}

// ============================================================================
// SYSTEM CONFIGURATION
// ============================================================================
static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART6_UART_Init(void); 
static void Error_Handler(void);

#define mainTIMER_FREQUENCY_MS pdMS_TO_TICKS(10UL)
static void TimerCallback(TimerHandle_t xTimerHandle)
{
	dmq::util::Timer::ProcessTimers();
}

// Highest priority so it can preempt any stalled task,
// including the FreeRTOS timer daemon that runs ProcessTimers().
static void WatchdogTask(void* pvParameters)
{
	for (;;) {
		dmq::os::Thread::WatchdogCheckAll();
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}

// ============================================================================
// MAIN TASK
// ============================================================================
void MainTask(void* pvParameters) {
    BSP_LED_On(LED4);
    printf("--- MainTask Started ---\n");

    TimerHandle_t xSystemTimer = xTimerCreate("SysTimer", mainTIMER_FREQUENCY_MS, pdTRUE, NULL, TimerCallback);
    if (xSystemTimer) xTimerStart(xSystemTimer, 0);

    #if defined(RUN_SIMPLE_TESTS)
        printf("Mode: SIMPLE TESTS\n");
        StartSimpleTests();
    #elif defined(RUN_STRESS_TESTS)
        printf("Mode: STRESS TESTS\n");
        StartStressTests();
    #elif defined(RUN_NETWORK_TESTS)
        printf("Mode: NETWORK TESTS\n");
        StartNetworkTests();
    #elif defined(RUN_DATABUS_TESTS)
        printf("Mode: DATABUS TESTS\n");
        StartDatabusTests();
    #else
        #error "Please select a test mode in main.cpp"
    #endif

    for(;;) {
        BSP_LED_Toggle(LED6);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART6_UART_Init(); 
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);

    BSP_LED_Init(LED3); BSP_LED_Init(LED4); BSP_LED_Init(LED5); BSP_LED_Init(LED6);
    BSP_LED_On(LED3);

    xTaskCreate(MainTask, "MainTask", 2048, NULL, 2, NULL);
    xTaskCreate(WatchdogTask, "Watchdog", 256, NULL, configMAX_PRIORITIES - 1, NULL);

    vTaskStartScheduler();

    while (1) { BSP_LED_Toggle(LED5); HAL_Delay(100); }
}

// ============================================================================
// PERIPHERAL INITIALIZATION
// ============================================================================

/**
  * @brief USART6 Initialization Function
  */
static void MX_USART6_UART_Init(void) 
{
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 115200;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
}

extern "C" void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance == USART6)
  {
    __HAL_RCC_USART6_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(USART6_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART6_IRQn);
  }
}

extern "C" {

    // 2. The CPU jumps here when USART6 fires
    void USART6_IRQHandler(void)
    {
        // 3. Call the HAL handler, which handles flags and calls RxCpltCallback
        HAL_UART_IRQHandler(&huart6);
    }

} // extern "C"

extern "C" void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{
  if(uartHandle->Instance == USART6)
  {
    __HAL_RCC_USART6_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_6 | GPIO_PIN_7);
  }
}

static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
}

extern "C" int __io_putchar(int ch) {
    ITM_SendChar(ch);
    return ch;
}

static void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_OscInitTypeDef RCC_OscInitStruct;

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);
  
  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;  
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;  
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);

  if (HAL_GetREVID() >= 0x1001)
  {
    __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
  }
}

static void Error_Handler(void)
{
  BSP_LED_On(LED5);
  while(1) {}
}

/* USER CODE BEGIN 4 */
extern "C" {

#define IDLE_STACK_SIZE 512
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize )
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ IDLE_STACK_SIZE ];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = IDLE_STACK_SIZE;
}

#define TIMER_STACK_SIZE 1024
void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer,
                                     StackType_t **ppxTimerTaskStackBuffer,
                                     uint32_t *pulTimerTaskStackSize )
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ TIMER_STACK_SIZE ];

    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = TIMER_STACK_SIZE;
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

} // extern "C"

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("CRITICAL: Stack Overflow in task: %s\n", pcTaskName);
    taskDISABLE_INTERRUPTS();
    for(;;);
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line)
{
  while (1) {}
}
#endif
