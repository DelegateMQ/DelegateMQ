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
void SysTick_Handler(void); // defined in main.cpp: increments g_ticks, drives Timer::ProcessTimers()

// --------------------------------------------------------------------------
// DEBUG PRINT HELPER
// Changed to 'static' to fix the "undefined reference" linker error
// --------------------------------------------------------------------------
static void debug_print(const char *msg) {
    // SYS_WRITE0 (0x04) prints a null-terminated string
    __asm volatile (
        "mov r0, #4\n"     // Operation 0x04
        "mov r1, %[str]\n" // Pointer to string
        "bkpt 0xAB\n"      // Semihosting Trigger
        : : [str] "r" (msg) : "r0", "r1", "memory"
    );
}

// --------------------------------------------------------------------------
// REAL RESET HANDLER
// --------------------------------------------------------------------------
void Reset_Handler(void)
{
    // 1. Enable FPU (Critical for Cortex-M4)
    volatile uint32_t *CPACR = (uint32_t *)0xE000ED88;
    *CPACR |= ((3UL << (10 * 2)) | (3UL << (11 * 2)));

    // 2. Print Trace (Proof that we are alive!)
    debug_print("--- ENTERING RESET HANDLER ---\n");

    // 3. Copy .data from FLASH to RAM
    uint32_t *src = &_etext;
    uint32_t *dst = &_data;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    // 4. Zero out .bss
    dst = &_bss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // 5. Initialize C++ Static Constructors
    debug_print("--- INITIALIZING C++ ---\n");
    __libc_init_array();

    // 6. Jump to Main
    debug_print("--- JUMPING TO MAIN ---\n");
    main();

    // 7. Loop forever if main returns
    while (1);
}

void Default_Handler(void) { while (1); }

__attribute__((section(".isr_vector")))
uint32_t *const vector_table[] = {
    (uint32_t *)&_stack_ptr,
    (uint32_t *)&Reset_Handler,
    (uint32_t *)&Default_Handler, // NMI
    (uint32_t *)&Default_Handler, // HardFault
    (uint32_t *)&Default_Handler, // MemManage
    (uint32_t *)&Default_Handler, // BusFault
    (uint32_t *)&Default_Handler, // UsageFault
    0, 0, 0, 0,
    (uint32_t *)&Default_Handler, // SVCall
    (uint32_t *)&Default_Handler, // Debug Monitor
    0,
    (uint32_t *)&Default_Handler, // PendSV
    (uint32_t *)&SysTick_Handler  // SysTick
};