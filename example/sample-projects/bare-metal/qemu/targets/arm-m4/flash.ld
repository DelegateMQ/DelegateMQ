/* flash.ld - Fixed Overlap Issues */
MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 4M
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 4M
}

SECTIONS
{
    /* 1. Vector Table (Must be at 0x0) */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* 2. Program Code & Read-Only Data */
    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        
        /* Keep standard system init/fini code */
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH

    /* 3. ARM Exception Tables (REQUIRED for C++) */
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx :
    {
        . = ALIGN(4);
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
        . = ALIGN(4);
    } > FLASH

    /* 4. C++ Constructors/Destructors (The Array Sections) */
    .preinit_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP (*(.preinit_array*))
        PROVIDE_HIDDEN (__preinit_array_end = .);
    } > FLASH

    .init_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array*))
        PROVIDE_HIDDEN (__init_array_end = .);
    } > FLASH

    .fini_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP (*(SORT(.fini_array.*)))
        KEEP (*(.fini_array*))
        PROVIDE_HIDDEN (__fini_array_end = .);
    } > FLASH

    /* 5. Mark the end of Flash data */
    . = ALIGN(4);
    _etext = .;

    /* 6. Initialized Data (Relocated from FLASH to RAM) */
    /* AT(_etext) means "Load this at the address _etext in Flash" */
    .data : AT (_etext)
    {
        . = ALIGN(4);
        _data = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    /* 7. Uninitialized Data (Zeroed in RAM) */
    .bss :
    {
        . = ALIGN(4);
        __bss_start__ = .;
        _bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
        _ebss = .;
        
        /* Heap Start (Aligned to 8 bytes for double/int64 access) */
        . = ALIGN(8);
        _end = .;
        __end__ = .;
    } > RAM

    /* 8. Stack Pointer (End of RAM) */
    _stack_ptr = ORIGIN(RAM) + LENGTH(RAM);
}