# DelegateMQ Bare-Metal RISC-V Example

This project demonstrates **DelegateMQ** on a **bare-metal RISC-V (RV32IMC)** target -- no RTOS, no threading -- built with the [xPack RISC-V Embedded GCC](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack) toolchain and run on QEMU's `virt` machine (`-bios none`, direct machine-mode boot -- no OpenSBI/firmware layer).

It's the RISC-V counterpart to `bare-metal-arm`: same feature set, same Timer-driven-by-a-real-hardware-interrupt test, adapted for RISC-V's different exception model (a single trap vector instead of ARM's per-exception NVIC, `mstatus.MIE`/CSRs instead of PRIMASK, CLINT `mtime`/`mtimecmp` instead of SysTick).

## Why this needs a different toolchain than `bare-metal-arm`

Ubuntu's `gcc-riscv64-unknown-elf` apt package is **compiler-only** -- no libc, no libstdc++, for this target. That's a hard blocker, not a nice-to-have: `Delegate<Sig>::Equal()` (used by `MulticastDelegate::Remove()`, exercised in Test 4) genuinely calls `dynamic_cast`, real RTTI, so a working `libstdc++`/`libsupc++` is required. Unlike ARM (`libstdc++-arm-none-eabi-picolibc` exists in Ubuntu's repos), there's no matching package for `riscv64-unknown-elf`.

**[xPack's RISC-V Embedded GCC](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases)** bundles a complete, matched newlib + libstdc++/libsupc++ build (like ATfE does for ARM) as a portable, no-root-needed tarball. This sample uses the `rv32imc/ilp32` multilib specifically (not `rv32imac`, which lacks a prebuilt `libstdc++` in this toolchain's release) -- I(base)+M(mul/div)+C(compressed), no atomics, soft-float ABI; a common baseline for real single-core embedded RV32 parts.

## Two real bootstrapping problems this surfaced

Getting a from-scratch RISC-V bare-metal boot working on QEMU's `virt` machine surfaced two genuine gotchas, both worth knowing if porting this further:

1. **The toolchain's generic `crt0.o` can't be used for a direct `-bios none` boot.** Its `_start` reads argc/argv straight off the stack at entry (`lw a0, 0(sp)`) -- it assumes some earlier boot stage (a firmware/SBI layer) already set up `sp` before jumping here, a hosted-semihosting convention. On a genuinely from-scratch boot, nothing has touched `sp` yet, so this crashes almost immediately (a store to a garbage stack address, then a fault-storm once `mtvec` -- itself still zero at that point -- redirects every subsequent trap back to address 0). Fixed by writing this sample's own `_start` (`startup.S`), the same role `bare-metal-arm/startup.c`'s `Reset_Handler` plays for ARM: set `sp`/`gp`, zero `.bss`, run C++ static constructors, call `main()`. Linked with `-nostartfiles` so the toolchain's own `crt0.o` is never pulled in.
2. **`libc.a` pulls in `_getentropy_r`** (used to seed things like `arc4random`) as part of its locale/startup machinery, even though nothing in this sample calls it. There's no hardware RNG reachable under a direct-boot `virt` machine (no SBI to ask), so `main.cpp` provides a stub `_getentropy()` returning `ENOSYS` -- the same "not available on this target" answer a real board with no TRNG peripheral would give.

## `Timer` on a real hardware interrupt (CLINT)

Test 6 programs the CLINT (Core Local Interruptor) machine timer -- `mtimecmp`/`mtime`, memory-mapped at `0x02000000` on QEMU's `virt` machine (confirmed via `qemu-system-riscv32 -M virt -machine dumpdtb=... ` + `dtc`: `clint@2000000`, `timebase-frequency = 10,000,000`) -- for a 1ms period, and installs a hand-written trap vector (`trap_entry.S` + `trap_dispatch()` in `main.cpp`) into `mtvec`. Unlike ARM's NVIC, RISC-V saves **nothing** automatically on trap entry -- `trap_entry.S` explicitly saves/restores the caller-saved register set around the call into C.

`trap_dispatch()` reads `mcause` and, for a genuine machine timer interrupt (`0x80000007`), increments `g_ticks` and calls `dmq::util::Timer::ProcessTimers()` directly from real ISR context -- exercising `dmq::os::BareMetalCriticalSection`'s RISC-V variant (`mstatus.MIE` save/clear/restore via `csrrci`/`csrs`, added alongside the existing ARM PRIMASK implementation) for real, the same verification `bare-metal-arm` already established for ARM. See `CLAUDE.md`'s "ISR-Safe Locking" section.

## Prerequisites

1. **[xPack RISC-V Embedded GCC](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases)** -- download the `linux-x64` (or matching platform) release tarball, extract anywhere, no install step.
   * Pass its path with `-DRISCV_TOOLCHAIN_PATH=<path>` at configure time.
2. **CMake** 3.16+ and **Ninja** (recommended).
3. **QEMU for RISC-V** -- on Debian/Ubuntu, `sudo apt install qemu-system-misc` (provides `qemu-system-riscv32`).

## Build Instructions

```bash
cmake -B build -G Ninja -DRISCV_TOOLCHAIN_PATH=/path/to/xpack-riscv-none-elf-gcc-15.2.0-1
cmake --build build
```

The output ELF is at `build/delegate_app`.

## Run Instructions

```bash
qemu-system-riscv32 -M virt -bios none -kernel build/delegate_app -nographic -semihosting-config enable=on,target=native
```

**Expected Output**

```txt
=========================================
   BARE METAL RISC-V DELEGATE SYSTEM ONLINE
=========================================

[Test 1] Unicast Delegate (Free Function):
  [Callback] FreeFunction called! Value: 100

[Test 2] Unicast Delegate (Lambda):
  [Callback] Lambda called! Capture: 42, Arg: 200

[Test 3] Multicast Delegate (Broadcast):
Firing all 3 targets...
  [Callback] FreeFunction called! Value: 300
  [Callback] MemberFunc called! Value: 300 (Instance: 0x...)
  [Callback] Multicast Lambda called! Val: 300

[Test 4] Removing a Delegate:
Firing remaining targets (Expected: 2)...
  [Callback] MemberFunc called! Value: 400 (Instance: 0x...)
  [Callback] Multicast Lambda called! Val: 400

[Test 5] Signals & Scoped Connections:
  -> Creating ScopedConnection inside block...
  -> Firing Signal (Expect Callback):
  [Callback] FreeFunction called! Value: 500
  -> Exiting block (ScopedConnection will destruct)...
  -> Firing Signal outside block (Expect NO Callback):

[Test 6] Timer Delegate (One-Shot):
  -> Starting Timer (200ms delay)...
  [Callback] Timer Expired!

=========================================
           ALL TESTS PASSED
=========================================
To Exit QEMU: Press Ctrl+a, release, then press x.
```

## Technical Details

| Setting | Value |
|---------|-------|
| Target | RV32IMC (`-march=rv32imc_zicsr -mabi=ilp32`) |
| C++ Standard | C++20 |
| RAM | 32 MB @ `0x80000000` (QEMU `virt`'s reported region is 128MB; the linker script reserves less for clarity) |
| CLINT | `0x02000000` (`mtimecmp` hart 0 @ `+0x4000`, `mtime` @ `+0xBFF8`), 10MHz timebase |
| Linker script | `virt.ld` -- defines `__global_pointer$` (gp-relative small-data addressing), `__bss_start`/`__bss_end`, and a 16KB stack |
| Startup | `startup.S` -- `_start`: sp/gp setup, `.bss` zero, `__libc_init_array()`, `main()` |
| Trap handling | `trap_entry.S` (register save/restore + `mret`) + `trap_dispatch()` in `main.cpp` (reads `mcause`, dispatches) |
| Semihosting | `--specs=semihost.specs` (xPack toolchain's `libsemihost.a` -- provides `_write`/`_sbrk`/etc.) |

## Related Examples

- [bare-metal-arm](../bare-metal-arm/) -- same concept on Cortex-M4 (ARM GCC + newlib, SysTick instead of CLINT, PRIMASK instead of `mstatus.MIE`)
- [atfe-armv7m-bare-metal](../atfe-armv7m-bare-metal/) -- Cortex-M3 with ATfE (Clang/LLVM)
