/// @file bsp.c
/// NUCLEO-H755ZI-Q, Cortex-M7: the board (LEDs, console UART) and bsp.h for
/// the test suite. bsp.h's timer interrupt is TIM6, a real NVIC interrupt, so
/// code it calls runs in genuine ISR context.

#include "board.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include <stdio.h>

// --------------------------------------------------------------------------
// LEDs
// --------------------------------------------------------------------------
#define LED_GREEN_PIN  GPIO_PIN_0     // LD1, PB0
#define LED_RED_PIN    GPIO_PIN_14    // LD3, PB14

static uint16_t LedPin(BoardLed led) { return led == LED_GREEN ? LED_GREEN_PIN : LED_RED_PIN; }

void BoardLedSet(BoardLed led, int on)
{
    HAL_GPIO_WritePin(GPIOB, LedPin(led), on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void BoardLedToggle(BoardLed led) { HAL_GPIO_TogglePin(GPIOB, LedPin(led)); }

static void BusyWaitMs(uint32_t ms)
{
    // Works with interrupts off: about 400 cycles per inner loop pass at 400 MHz
    for (volatile uint32_t i = 0; i < ms * 100000u; i++) {}
}

void BoardFatal(void)
{
    __disable_irq();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = LED_RED_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, LED_RED_PIN, GPIO_PIN_SET);
    for (;;) {}
}

// --------------------------------------------------------------------------
// Console: USART3 on the ST-LINK virtual COM port
// --------------------------------------------------------------------------
static UART_HandleTypeDef s_console;
static volatile int s_consoleReady;
static SemaphoreHandle_t s_consoleMutex;
static StaticSemaphore_t s_consoleMutexBuf;

void BoardInit(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = LED_GREEN_PIN | LED_RED_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);

    g.Pin = GPIO_PIN_8 | GPIO_PIN_9;    // USART3 TX, RX
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &g);

    s_console.Instance = USART3;
    s_console.Init.BaudRate = BOARD_CONSOLE_BAUD;
    s_console.Init.WordLength = UART_WORDLENGTH_8B;
    s_console.Init.StopBits = UART_STOPBITS_1;
    s_console.Init.Parity = UART_PARITY_NONE;
    s_console.Init.Mode = UART_MODE_TX_RX;
    s_console.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_console.Init.OverSampling = UART_OVERSAMPLING_16;
    s_console.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    if (HAL_UART_Init(&s_console) != HAL_OK)
        BoardFatal();
    s_consoleReady = 1;

    s_consoleMutex = xSemaphoreCreateMutexStatic(&s_consoleMutexBuf);
}

// Polled writes to the USART's registers, not HAL_UART_Transmit(): HAL's
// state and lock would drop the output of a fault that interrupts a print
// (HAL_BUSY). This works from any context, interrupts on or off.
static void ConsoleSend(const char* buf, int len)
{
    if (!s_consoleReady)
        return;     // before BoardInit(): nothing to send on
    for (int i = 0; i < len; i++) {
        while (!(USART3->ISR & USART_ISR_TXE_TXFNF)) {}
        USART3->TDR = (uint8_t)buf[i];
    }
}

/// Wait until the last byte has left the wire
static void ConsoleFlush(void)
{
    if (s_consoleReady)
        while (!(USART3->ISR & USART_ISR_TC)) {}
}

/// An ordinary task, free to block: not an interrupt, and interrupts not
/// masked by PRIMASK or by BASEPRI (taskDISABLE_INTERRUPTS() and FreeRTOS
/// critical sections use BASEPRI on this port).
static int InBlockingTaskContext(void)
{
    return __get_IPSR() == 0 && __get_PRIMASK() == 0 && __get_BASEPRI() == 0 &&
           xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

int BoardWrite(const char* buf, int len)
{
    const int lock = s_consoleMutex && InBlockingTaskContext();
    if (lock)
        xSemaphoreTake(s_consoleMutex, portMAX_DELAY);

    // "\n" -> "\r\n" for terminals
    int start = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            if (i > start)
                ConsoleSend(buf + start, i - start);
            ConsoleSend("\r\n", 2);
            start = i + 1;
        }
    }
    if (len > start)
        ConsoleSend(buf + start, len - start);

    if (lock)
        xSemaphoreGive(s_consoleMutex);
    return len;
}

// --------------------------------------------------------------------------
// bsp.h for the test suite
// --------------------------------------------------------------------------
static void (*s_handler)(void);

const char* BspName(void)
{
    return "NUCLEO-H755ZI-Q: Cortex-M7 at 400 MHz, FreeRTOS GCC/ARM_CM4F";
}

void BspStartTimerIsr(uint32_t hz, void (*handler)(void))
{
    // TIM6 is 16 bits: at a 1 MHz count, 16 Hz to 1 MHz
    if (hz < 16u || hz > 1000000u) {
        printf("BspStartTimerIsr: %lu Hz is out of range (16 Hz to 1 MHz)\n", (unsigned long)hz);
        BspExit(3);
    }
    s_handler = handler;
    __HAL_RCC_TIM6_CLK_ENABLE();
    // APB1 is divided by 2, so the timers run at twice PCLK1 (200 MHz)
    const uint32_t timerClock = HAL_RCC_GetPCLK1Freq() * 2u;
    TIM6->CR1 = 0;
    TIM6->PSC = timerClock / 1000000u - 1u;    // 1 MHz count
    TIM6->ARR = 1000000u / hz - 1u;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0;
    TIM6->DIER = TIM_DIER_UIE;
    // 6: below configMAX_SYSCALL_INTERRUPT_PRIORITY (5), so it may use the
    // FromISR API and a task's critical section masks it
    NVIC_SetPriority(TIM6_DAC_IRQn, 6);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;
}

void BspStopTimerIsr(void)
{
    TIM6->CR1 = 0;
    TIM6->DIER = 0;
    NVIC_DisableIRQ(TIM6_DAC_IRQn);
    TIM6->SR = 0;
    NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
}

void TIM6_DAC_IRQHandler(void)
{
    if (TIM6->SR & TIM_SR_UIF) {
        TIM6->SR = 0;   // UIF is TIM6's only flag
        if (s_handler)
            s_handler();
    }
}

/// End of the run. Prints a line run.py looks for, then shows the result on
/// the LEDs: green on for a pass, red blinking for a failure.
void BspExit(int status)
{
    if (InBlockingTaskContext())
        fflush(stdout);

    char line[96];
    int n = snprintf(line, sizeof line, "\nBSP EXIT %d (FreeRTOS heap: min free %u bytes)\n",
                     status, (unsigned)xPortGetMinimumEverFreeHeapSize());
    __disable_irq();
    BoardWrite(line, n);    // interrupts off: direct, no mutex
    ConsoleFlush();

    BoardLedSet(LED_GREEN, status == 0);
    BoardLedSet(LED_RED, status != 0);
    for (;;) {
        if (status != 0) {
            BusyWaitMs(150);
            BoardLedToggle(LED_RED);
        }
    }
}
