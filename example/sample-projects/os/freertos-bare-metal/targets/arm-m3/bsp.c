/// @file bsp.c
/// Cortex-M3 on QEMU mps2-an385: bsp.h's hardware timer interrupt is the
/// CMSDK APB timer 0 (0x40000000, IRQ 8, 25 MHz), a real NVIC interrupt, so
/// code it calls runs in genuine ISR context (IPSR != 0).

#include "bsp.h"
#include <stdlib.h>

#define TIMER0_BASE       0x40000000u
#define TIMER0_CTRL       (*(volatile uint32_t*)(TIMER0_BASE + 0x00))
#define TIMER0_VALUE      (*(volatile uint32_t*)(TIMER0_BASE + 0x04))
#define TIMER0_RELOAD     (*(volatile uint32_t*)(TIMER0_BASE + 0x08))
#define TIMER0_INTCLEAR   (*(volatile uint32_t*)(TIMER0_BASE + 0x0C))
#define TIMER_CTRL_ENABLE (1u << 0)
#define TIMER_CTRL_IRQEN  (1u << 3)
#define TIMER0_IRQ        8u
#define TIMER_CLOCK_HZ    25000000u

#define NVIC_ISER0        (*(volatile uint32_t*)0xE000E100)
#define NVIC_ICER0        (*(volatile uint32_t*)0xE000E180)
#define NVIC_IPR          ((volatile uint8_t*)0xE000E400)

// Priority 5 (of 0-7, top 3 bits): below configMAX_SYSCALL_INTERRUPT_PRIORITY
// (2), so it may use the FromISR API and a task's critical section masks it.
#define TIMER0_PRIORITY   (5u << 5)

static void (*s_handler)(void);

const char* BspName(void) { return "Cortex-M3, FreeRTOS GCC/ARM_CM3, QEMU mps2-an385"; }

void BspStartTimerIsr(uint32_t hz, void (*handler)(void))
{
    s_handler = handler;
    TIMER0_CTRL = 0;
    TIMER0_RELOAD = TIMER_CLOCK_HZ / hz - 1;
    TIMER0_VALUE = TIMER_CLOCK_HZ / hz - 1;
    TIMER0_INTCLEAR = 1;
    NVIC_IPR[TIMER0_IRQ] = TIMER0_PRIORITY;
    NVIC_ISER0 = 1u << TIMER0_IRQ;
    TIMER0_CTRL = TIMER_CTRL_ENABLE | TIMER_CTRL_IRQEN;
}

void BspStopTimerIsr(void)
{
    TIMER0_CTRL = 0;
    NVIC_ICER0 = 1u << TIMER0_IRQ;
    TIMER0_INTCLEAR = 1;
}

// _Exit -> retarget.c's _exit -> semihosting SYS_EXIT with the status
void BspExit(int status) { _Exit(status); }

void TIMER0_IRQHandler(void)
{
    TIMER0_INTCLEAR = 1;
    if (s_handler)
        s_handler();
}
