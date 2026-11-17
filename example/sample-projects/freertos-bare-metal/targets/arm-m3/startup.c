/// @file startup.c
/// Cortex-M3 (QEMU mps2-an385) startup for FreeRTOS: vector table with the
/// kernel's SVC, PendSV and SysTick handlers (its port checks they are
/// installed), the CMSDK TIMER0 interrupt (bsp.c), and fault handlers that
/// report through semihosting and end the run instead of hanging.

#include <stdint.h>

extern uint32_t _stack_ptr;
extern uint32_t _etext;
extern uint32_t _data;
extern uint32_t _edata;
extern uint32_t _bss;
extern uint32_t _ebss;

extern int main(void);
extern void __libc_init_array(void);

// FreeRTOS GCC/ARM_CM3 port
extern void vPortSVCHandler(void);
extern void xPortPendSVHandler(void);
extern void xPortSysTickHandler(void);

void TIMER0_IRQHandler(void); // bsp.c

void Reset_Handler(void);
void Fault_Handler(void);
void Default_Handler(void);

static void semihost_write0(const char *str)
{
    register int r0 __asm("r0") = 0x04;     // SYS_WRITE0
    register const char *r1 __asm("r1") = str;
    __asm volatile ("bkpt 0xAB" : : "r" (r0), "r" (r1) : "memory");
}

static void semihost_exit_failure(void)
{
    register int r0 __asm("r0") = 0x18;     // SYS_EXIT
    register int r1 __asm("r1") = 0x20023;  // ADP_Stopped_RunTimeErrorUnknown: QEMU exits with 1
    __asm volatile ("bkpt 0xAB" : : "r" (r0), "r" (r1) : "memory");
}

void Reset_Handler(void)
{
    // Copy .data from flash to RAM, zero .bss
    uint32_t *src = &_etext;
    uint32_t *dst = &_data;
    while (dst < &_edata)
        *dst++ = *src++;
    dst = &_bss;
    while (dst < &_ebss)
        *dst++ = 0;

    __libc_init_array();    // C++ static constructors
    main();
    while (1);
}

void Fault_Handler(void)
{
    semihost_write0("\nCPU FAULT (HardFault, MemManage, BusFault or UsageFault)\n");
    semihost_exit_failure();
    while (1);
}

void Default_Handler(void)
{
    semihost_write0("\nUNEXPECTED INTERRUPT\n");
    semihost_exit_failure();
    while (1);
}

// Core exceptions, then the first 16 MPS2 AN385 interrupts (TIMER0 is IRQ 8)
__attribute__((section(".isr_vector")))
void (*const vector_table[16 + 16])(void) = {
    (void (*)(void))&_stack_ptr,
    Reset_Handler,
    Default_Handler,        // NMI
    Fault_Handler,          // HardFault
    Fault_Handler,          // MemManage
    Fault_Handler,          // BusFault
    Fault_Handler,          // UsageFault
    0, 0, 0, 0,
    vPortSVCHandler,        // SVCall: starts the first task
    Default_Handler,        // Debug Monitor
    0,
    xPortPendSVHandler,     // PendSV: context switch
    xPortSysTickHandler,    // SysTick: the RTOS tick
    // IRQ 0-7: UART0-2 RX/TX, ...
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    TIMER0_IRQHandler,      // IRQ 8: CMSDK APB timer 0
    // IRQ 9-15
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler,
};
