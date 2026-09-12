/* trap_entry.S -- RISC-V machine-mode trap entry point.
 *
 * Unlike ARM's NVIC (hardware auto-saves a register frame on exception
 * entry, per vector-table slot), RISC-V has a single trap vector (mtvec)
 * and saves NOTHING automatically -- software must save every register the
 * C calling convention doesn't already guarantee the callee preserves
 * before calling into any C function, and restore them before `mret`.
 *
 * Only the caller-saved set needs saving here (ra, t0-t2, a0-a7, t3-t6):
 * trap_dispatch() (see main.cpp) is an ordinary C function and will itself
 * preserve every callee-saved register (s0-s11) exactly like any function
 * call would, so this entry stub doesn't need to touch those.
 */

.section .text
.align 4
.global trap_entry
trap_entry:
    addi sp, sp, -64
    sw   ra,  0(sp)
    sw   t0,  4(sp)
    sw   t1,  8(sp)
    sw   t2,  12(sp)
    sw   a0,  16(sp)
    sw   a1,  20(sp)
    sw   a2,  24(sp)
    sw   a3,  28(sp)
    sw   a4,  32(sp)
    sw   a5,  36(sp)
    sw   a6,  40(sp)
    sw   a7,  44(sp)
    sw   t3,  48(sp)
    sw   t4,  52(sp)
    sw   t5,  56(sp)
    sw   t6,  60(sp)

    call trap_dispatch

    lw   ra,  0(sp)
    lw   t0,  4(sp)
    lw   t1,  8(sp)
    lw   t2,  12(sp)
    lw   a0,  16(sp)
    lw   a1,  20(sp)
    lw   a2,  24(sp)
    lw   a3,  28(sp)
    lw   a4,  32(sp)
    lw   a5,  36(sp)
    lw   a6,  40(sp)
    lw   a7,  44(sp)
    lw   t3,  48(sp)
    lw   t4,  52(sp)
    lw   t5,  56(sp)
    lw   t6,  60(sp)
    addi sp, sp, 64

    mret
