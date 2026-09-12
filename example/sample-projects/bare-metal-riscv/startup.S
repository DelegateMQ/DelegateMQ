/* startup.S -- RISC-V entry point for QEMU's "virt" machine, -bios none.
 *
 * The xPack toolchain's generic crt0.o turns out NOT to be usable for a
 * direct-boot (-bios none -kernel) target: its _start reads argc/argv
 * straight off the stack at entry (`lw a0, 0(sp)`), which means it assumes
 * some earlier boot stage already set up sp/argc/argv before jumping here
 * (a hosted-semihosting convention) -- not true for a from-scratch QEMU
 * "virt" boot, where nothing has touched sp yet. So this sample supplies
 * its own _start instead, the same role bare-metal-arm/startup.c's
 * Reset_Handler plays for ARM: set up sp/gp, zero .bss, run C++ static
 * constructors, call main(). CMakeLists.txt links with -nostartfiles so
 * this _start is the only one -- crt0.o's is never pulled in.
 */

.section .text.init
.global _start
_start:
    /* gp (global pointer) must be set with relaxation disabled -- otherwise
     * the linker could "relax" this load into a gp-relative reference to
     * itself, which is circular. Same requirement crt0.o's own (unusable
     * for other reasons) _start observed. */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    la sp, _stack_top

    /* Zero .bss ([__bss_start, __bss_end), per virt.ld) one word at a time
     * -- avoids depending on libc's memset() this early, before anything
     * else has run. */
    la   t0, __bss_start
    la   t1, __bss_end
1:
    bge  t0, t1, 2f
    sw   zero, 0(t0)
    addi t0, t0, 4
    j    1b
2:

    /* C++ static constructors (registered in .init_array by virt.ld). */
    call __libc_init_array

    call main

    /* main() is declared to return int and never does in this sample, but
     * if it ever did, there is nowhere to return to on bare metal. */
3:
    j 3b
