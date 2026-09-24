/**
 * @file controller/platform/f4/main.cpp
 * @brief Pumptron controller -- STM32F4 Discovery target (FreeRTOS, bare hardware).
 *
 * Runs the exact same PumpController as the FreeRTOS simulator target, with
 * two substitutions made here and nowhere else:
 *   - F4Board (LIS3DSH, die temp sensor, user button, LEDs) instead of SimBoard
 *   - SerialLink<Stm32UartTransport> on USART6 (RS-232 via the STM32F4DIS-BB
 *     base board) instead of NetworkNode<UDP>
 *
 * Wiring: USART6 TX = PC6, RX = PC7, 115200 8N1. printf() goes to SWV ITM.
 *
 * Memory layout:
 *   - FreeRTOS heap (heap_4, 64 KB) in CCM RAM (no DMA is used, so CCM is fine)
 *   - All task stacks statically allocated in main SRAM
 */

#include "stm32f4xx_hal.h"
#include "stm32f4_discovery.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "DelegateMQ.h"
#include "pump/PumpController.h"
#include "pump/LinkErrorReporter.h"
#include "util/SerialLink.h"
#include "util/Topology.h"
#include "F4Board.h"

#include <cstdio>
#include <new>

using namespace pumptron;

using ControllerLink = SerialLink<dmq::transport::Stm32UartTransport>;

// ---------------------------------------------------------------------------
// Globals required by Stm32UartTransport / the HAL
// ---------------------------------------------------------------------------
dmq::transport::Stm32UartTransport* g_uartTransportInstance = nullptr;

extern "C" {
UART_HandleTypeDef huart6;
}

// ---------------------------------------------------------------------------
// FreeRTOS heap in CCM RAM (configAPPLICATION_ALLOCATED_HEAP = 1)
// ---------------------------------------------------------------------------
extern "C" {
__attribute__((section(".ccmram"), aligned(8))) uint8_t ucHeap[configTOTAL_HEAP_SIZE];
}

// Route all C++ allocation (DelegateMQ messages, xallocator blocks, std
// containers) through the thread-safe FreeRTOS heap.
void* operator new  (size_t n)                                 { void* p = pvPortMalloc(n); configASSERT(p); return p; }
void* operator new[](size_t n)                                 { void* p = pvPortMalloc(n); configASSERT(p); return p; }
void* operator new  (size_t n, const std::nothrow_t&) noexcept { return pvPortMalloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return pvPortMalloc(n); }
void  operator delete  (void* p)                       noexcept { vPortFree(p); }
void  operator delete[](void* p)                       noexcept { vPortFree(p); }
void  operator delete  (void* p, size_t)               noexcept { vPortFree(p); }
void  operator delete[](void* p, size_t)               noexcept { vPortFree(p); }

// ---------------------------------------------------------------------------
// Static task stacks (words)
// ---------------------------------------------------------------------------
static constexpr uint32_t STARTUP_STACK_WORDS = 1024;
static constexpr uint32_t PUMP_STACK_WORDS    = 1536;
static constexpr uint32_t LINK_TX_STACK_WORDS = 1536;
static constexpr uint32_t LINK_RX_STACK_WORDS = 1536;

static StackType_t  s_startupStack[STARTUP_STACK_WORDS];
static StaticTask_t s_startupTcb;
static StackType_t  s_pumpStack[PUMP_STACK_WORDS];
static StackType_t  s_linkTxStack[LINK_TX_STACK_WORDS];
static StackType_t  s_linkRxStack[LINK_RX_STACK_WORDS];
static StaticTask_t s_linkRxTcb;

static ControllerLink* s_link = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void SystemClock_Config();
static void MX_USART6_UART_Init();
static void Error_Handler();

// ---------------------------------------------------------------------------
// Tasks
// ---------------------------------------------------------------------------

/// Drains incoming frames. Stm32UartTransport::Receive() sleeps on a semaphore
/// given by the USART6 RX interrupt, so this task costs nothing while idle.
static void LinkRxTask(void*)
{
    for (;;)
        s_link->Poll(1);
}

/// Builds the application after the scheduler is running (dmq::os::Thread and
/// the UART transport create FreeRTOS objects), then becomes the watchdog.
static void StartupTask(void*)
{
    static board::F4Board board;
    static pump::PumpController pumpController(board);
    static ControllerLink link;
    s_link = &link;

    // DataBus + link errors (incl. send-queue drops) -> LINK_DEGRADED alarm /
    // status resync. Blinks the orange LED while active.
    static pump::LinkErrorReporter errorReporter(link, pumpController);
    errorReporter.WatchSendDropped(link.OnSendDropped);

    if (link.GetTransport().Create(&huart6) != 0) {
        printf("Controller: ERROR - USART6 transport init failed\n");
        Error_Handler();
    }

    ConfigureControllerLink(link);
    link.GetSendThread().SetStackMem(s_linkTxStack, LINK_TX_STACK_WORDS);
    link.GetSendThread().SetThreadPriority(PRIORITY_LINK);
    link.Start("GUI", ControllerLink::RecvMode::EXTERNAL_POLL, WATCHDOG_TIMEOUT);

    xTaskCreateStatic(LinkRxTask, "LinkRx", LINK_RX_STACK_WORDS, nullptr, PRIORITY_LINK,
                      s_linkRxStack, &s_linkRxTcb);

    pumpController.GetThread().SetStackMem(s_pumpStack, PUMP_STACK_WORDS);
    pumpController.GetThread().SetThreadPriority(PRIORITY_PUMP);
    pumpController.Start(WATCHDOG_TIMEOUT);

    printf("Pumptron controller (STM32F4 Discovery) running, USART6 %d baud\n", SERIAL_BAUD);

    for (;;) {
        dmq::os::Thread::WatchdogCheckAll();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void SysTimerCallback(TimerHandle_t)
{
    dmq::util::Timer::ProcessTimers();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main()
{
    HAL_Init();
    SystemClock_Config();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    MX_USART6_UART_Init();
    Logger_Init();

    printf("Pumptron controller starting (STM32F4 Discovery)...\n");

    TimerHandle_t sysTimer = xTimerCreate("SysTimer",
        pdMS_TO_TICKS(std::chrono::duration_cast<std::chrono::milliseconds>(TIMER_TICK_PERIOD).count()),
        pdTRUE, nullptr, SysTimerCallback);
    xTimerStart(sysTimer, 0);

    xTaskCreateStatic(StartupTask, "Startup", STARTUP_STACK_WORDS, nullptr, configMAX_PRIORITIES - 1,
                      s_startupStack, &s_startupTcb);

    vTaskStartScheduler();
    Error_Handler();   // only reached if the scheduler could not start
}

// ---------------------------------------------------------------------------
// USART6 (RS-232 on the STM32F4DIS-BB base board)
// ---------------------------------------------------------------------------
static void MX_USART6_UART_Init()
{
    huart6.Instance = USART6;
    huart6.Init.BaudRate = SERIAL_BAUD;
    huart6.Init.WordLength = UART_WORDLENGTH_8B;
    huart6.Init.StopBits = UART_STOPBITS_1;
    huart6.Init.Parity = UART_PARITY_NONE;
    huart6.Init.Mode = UART_MODE_TX_RX;
    huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart6.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart6) != HAL_OK)
        Error_Handler();
}

extern "C" void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{
    if (uartHandle->Instance != USART6)
        return;

    __HAL_RCC_USART6_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(GPIOC, &gpio);

    // Must be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5):
    // the RX ISR calls xSemaphoreGiveFromISR().
    HAL_NVIC_SetPriority(USART6_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART6_IRQn);
}

extern "C" void USART6_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart6);
}

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart)
{
    if (g_uartTransportInstance && huart->Instance == USART6)
        g_uartTransportInstance->OnRxCplt();
}

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
    // An overrun aborts the HAL's interrupt-driven receive; OnRxError() (ISR-safe,
    // no OS calls) clears the error flags and re-arms it.
    if (g_uartTransportInstance && huart->Instance == USART6)
        g_uartTransportInstance->OnRxError();
}

// ---------------------------------------------------------------------------
// Clock: HSE 8 MHz -> PLL -> 168 MHz SYSCLK, APB1 42 MHz, APB2 84 MHz
// ---------------------------------------------------------------------------
static void SystemClock_Config()
{
    RCC_OscInitTypeDef osc = {};
    RCC_ClkInitTypeDef clk = {};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        Error_Handler();

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK)
        Error_Handler();

    if (HAL_GetREVID() >= 0x1001)
        __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
}

// ---------------------------------------------------------------------------
// Fault handling: red LED on, halt
// ---------------------------------------------------------------------------
static void Error_Handler()
{
    __disable_irq();
    BSP_LED_Init(LED5);
    BSP_LED_On(LED5);
    for (;;) {}
}

extern "C" void vAssertCalled(const char* pcFile, unsigned long ulLine)
{
    printf("ASSERT %s:%lu\n", pcFile, ulLine);
    Error_Handler();
}

extern "C" void vApplicationMallocFailedHook(void)
{
    printf("FreeRTOS: heap exhausted (%u bytes free)\n", static_cast<unsigned>(xPortGetFreeHeapSize()));
    Error_Handler();
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char* pcTaskName)
{
    printf("FreeRTOS: stack overflow in task '%s'\n", pcTaskName);
    Error_Handler();
}

extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t** tcb, StackType_t** stack, configSTACK_DEPTH_TYPE* size)
{
    static StaticTask_t t;
    static StackType_t s[configMINIMAL_STACK_SIZE];
    *tcb = &t; *stack = s; *size = configMINIMAL_STACK_SIZE;
}

extern "C" void vApplicationGetTimerTaskMemory(StaticTask_t** tcb, StackType_t** stack, configSTACK_DEPTH_TYPE* size)
{
    static StaticTask_t t;
    static StackType_t s[configTIMER_TASK_STACK_DEPTH];
    *tcb = &t; *stack = s; *size = configTIMER_TASK_STACK_DEPTH;
}

#ifdef USE_FULL_ASSERT
extern "C" void assert_failed(uint8_t* file, uint32_t line)
{
    vAssertCalled(reinterpret_cast<const char*>(file), line);
}
#endif
