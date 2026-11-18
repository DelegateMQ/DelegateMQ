/// @file startup.c
/// Cortex-M0 (nRF51822) startup: vector table, reset handler, and a HardFault
/// handler that reports through semihosting and ends the run. Armv6-M faults
/// on an unaligned load/store, and every fault lands in HardFault (there is
/// no MemManage/BusFault/UsageFault), so a fault fails the test run at once
/// instead of hanging until the runner's timeout.

#include <stdint.h>

extern uint32_t _stack_ptr;
extern uint32_t _etext;
extern uint32_t _data;
extern uint32_t _edata;
extern uint32_t _bss;
extern uint32_t _ebss;

extern int main(void);
extern void __libc_init_array(void);

void Reset_Handler(void);
void Default_Handler(void);
void HardFault_Handler(void);
void TIMER0_IRQHandler(void); // defined in bsp.cpp: the 1 ms tick

static void semihost_write0(const char *str)
{
    register int r0 __asm("r0") = 0x04; // SYS_WRITE0
    register const char *r1 __asm("r1") = str;
    __asm volatile ("bkpt 0xAB" : : "r" (r0), "r" (r1) : "memory");
}

static void semihost_exit_failure(void)
{
    register int r0 __asm("r0") = 0x18;      // SYS_EXIT
    register int r1 __asm("r1") = 0x20023;   // ADP_Stopped_RunTimeErrorUnknown: QEMU exits with 1
    __asm volatile ("bkpt 0xAB" : : "r" (r0), "r" (r1) : "memory");
}

void Reset_Handler(void)
{
    // Copy .data from flash to RAM
    uint32_t *src = &_etext;
    uint32_t *dst = &_data;
    while (dst < &_edata)
        *dst++ = *src++;

    // Zero .bss
    dst = &_bss;
    while (dst < &_ebss)
        *dst++ = 0;

    // C++ static constructors
    __libc_init_array();

    main();
    while (1);
}

void HardFault_Handler(void)
{
    semihost_write0("\nHARD FAULT (Cortex-M0: unaligned access, bad address or undefined instruction)\n");
    semihost_exit_failure();
    while (1);
}

void Default_Handler(void)
{
    semihost_write0("\nUNEXPECTED INTERRUPT\n");
    semihost_exit_failure();
    while (1);
}

// Core exceptions, then the nRF51's 32 peripheral interrupts (TIMER0 is IRQ 8)
__attribute__((section(".isr_vector")))
void (*const vector_table[16 + 32])(void) = {
    (void (*)(void))&_stack_ptr,
    Reset_Handler,
    Default_Handler,    // NMI
    HardFault_Handler,
    0, 0, 0, 0, 0, 0, 0, // reserved on Armv6-M
    Default_Handler,    // SVCall
    0, 0,               // reserved
    Default_Handler,    // PendSV
    Default_Handler,    // SysTick (not on the nRF51)
    // IRQ 0-7: POWER_CLOCK, RADIO, UART0, SPI0/TWI0, SPI1/TWI1, -, GPIOTE, ADC
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    TIMER0_IRQHandler,  // IRQ 8
    // IRQ 9-31
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler,
};
