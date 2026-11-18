/* nrf51.ld: nRF51822 (BBC micro:bit, QEMU microbit) memory layout.
   256 KB flash at 0x00000000, 16 KB RAM at 0x20000000. The stack starts at
   the top of RAM and grows down; the heap (retarget.c's _sbrk) grows up from
   _end, after .bss. */
MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 256K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 16K
}

/* Stack reserved at the top of RAM: the link fails if .data + .bss + this
   don't fit, rather than the stack running into them at run time. */
_stack_size = 4K;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH

    /* C++ exception unwinding tables */
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx :
    {
        . = ALIGN(4);
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
        . = ALIGN(4);
    } > FLASH

    /* C++ static constructors/destructors, run by __libc_init_array */
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

    . = ALIGN(4);
    _etext = .;

    /* Initialized data: stored in flash at _etext, copied to RAM by startup.c */
    .data : AT (_etext)
    {
        . = ALIGN(4);
        _data = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    /* Zeroed by startup.c */
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
        . = ALIGN(8);
        _end = .;
        __end__ = .;
    } > RAM

    _stack_ptr = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(_end + _stack_size <= _stack_ptr, "RAM overflow: .data + .bss + stack don't fit in 16 KB")
}
