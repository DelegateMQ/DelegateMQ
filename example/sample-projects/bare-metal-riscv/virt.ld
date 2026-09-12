/* Linker script for QEMU's "virt" RISC-V machine (RV32), no bootloader --
 * "-bios none -kernel delegate_app" loads this ELF directly and jumps to
 * its entry point (_start, provided by the toolchain's generic crt0.o) in
 * machine mode. RAM-only: QEMU's virt machine reports a single memory
 * region at 0x80000000 (confirmed via `-machine dumpdtb` + dtc), so there's
 * no separate flash/RAM split to manage the way bare-metal-arm's flash.ld
 * has -- .data is loaded directly at its runtime address, no copy-down step
 * needed.
 */

ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 32M
}

SECTIONS
{
    .text : {
        *(.text.init)
        *(.text .text.*)
    } > RAM

    .rodata : {
        *(.rodata .rodata.*)
    } > RAM

    /* C++ static constructor pointers -- __libc_init_array() (called by
     * crt0.o before main()) walks this table. */
    .init_array : {
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);
    } > RAM

    .fini_array : {
        PROVIDE(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE(__fini_array_end = .);
    } > RAM

    .data : {
        *(.data .data.*)
    } > RAM

    /* __global_pointer$ -- crt0.o's _start loads `gp` from this symbol
     * (undefined-reference at link time otherwise); RISC-V code addresses
     * anything within +/-2KB of gp with a single gp-relative load/store
     * instead of a multi-instruction absolute address, which is why small
     * data lives here. The "+ 0x800" (2KB) centers gp in that range. */
    .sdata : {
        __global_pointer$ = . + 0x800;
        *(.srodata.cst16) *(.srodata.cst8) *(.srodata.cst4) *(.srodata.cst2) *(.srodata*)
        *(.sdata .sdata.* .gnu.linkonce.s.*)
    } > RAM

    /* crt0.o's boot code zeroes [__bss_start, _end) -- both names are its
     * own undefined-symbol expectations (checked via `nm crt0.o`), the
     * standard newlib convention. .sbss (small-data BSS, same gp-relative
     * addressing as .sdata above) must be zeroed the same way, so it's
     * kept contiguous with .bss here. */
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.sbss .sbss.* .gnu.linkonce.sb.*)
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    . = ALIGN(16);
    . += 0x4000; /* 16KB stack */
    PROVIDE(_stack_top = .);

    /* _end is libsemihost.a's _sbrk() heap-start marker; everything from
     * here to the end of RAM is available for the C++ heap (Signal's
     * internal state, etc. -- same role retarget.c's _sbrk played in
     * bare-metal-arm). */
    . = ALIGN(16);
    PROVIDE(_end = .);
    PROVIDE(end = .);
}
