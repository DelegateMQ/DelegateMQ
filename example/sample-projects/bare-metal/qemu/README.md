# DelegateMQ Bare-Metal Example

DelegateMQ on bare metal: **no RTOS and no threads** (`DMQ_THREAD_NONE`), on several CPUs, all runnable in QEMU. The tests in `main.cpp` are the same on every target. Each folder in `targets/` supplies only what differs: toolchain, startup code, linker script, and a 1 ms hardware timer interrupt.

| Target | CPU | Toolchain | C / C++ library | Timer interrupt | QEMU machine |
|---|---|---|---|---|---|
| `arm-m4` (default) | Cortex-M4 with FPU (Armv7E-M) | Arm GNU Toolchain (`arm-none-eabi-gcc`) | newlib-nano / libstdc++ | SysTick | `mps2-an386` |
| `arm-m0` | Cortex-M0 (Armv6-M), nRF51822, 16 KB RAM | Arm GNU Toolchain (`arm-none-eabi-gcc`) | newlib-nano / libstdc++ | nRF51 TIMER0 | `microbit` |
| `arm-m3-atfe` | Cortex-M3 (Armv7-M), soft float | Arm Toolchain for Embedded (ATfE: clang, lld) | picolibc / libc++ | SysTick | `mps2-an385` |
| `riscv32` | RV32IMC | xPack RISC-V Embedded GCC | newlib / libstdc++ | CLINT `mtime`/`mtimecmp` | `virt` |

## The Tests

1. Unicast delegate to a free function
2. Unicast delegate to a lambda with a capture
3. Multicast delegate: free function, member function and lambda
4. Removing a delegate from a multicast delegate (uses `Delegate::Equal()`, which needs RTTI)
5. `Signal` with a `ScopedConnection`: connected inside a block, disconnected when the block ends
6. One-shot `dmq::util::Timer`, fired by the **real timer interrupt**: its handler increments `g_ticks` (read by `dmq::os::BareMetalClock`) and calls `Timer::ProcessTimers()` from interrupt context. That exercises `dmq::os::BareMetalCriticalSection` for real, PRIMASK on Arm and `mstatus.MIE` on RISC-V (see CLAUDE.md's "ISR-Safe Locking").

Each test checks its own results. The program prints `ALL TESTS PASSED` and exits with status 0, or prints a `FAIL:` line per failed check and exits with status 1. Output and exit go through semihosting, so under QEMU the program's exit status becomes QEMU's own, which is what `scripts/05_run_samples.py` checks.

## Layout

```
bare-metal/
├── main.cpp            the tests (every target)
├── bsp.h               what a target provides: BspName(), BspStartTick(), BspExit()
├── CMakeLists.txt      -DBM_TARGET=<name> selects targets/<name>/
├── Makefile            arm-m4 without CMake (see below)
├── config/             DelegateMQ options for the Makefile build
└── targets/
    ├── common/         cortex_m_systick.h (Arm SysTick targets), retarget.c (newlib syscalls: arm-m4, arm-m0)
    ├── arm-m4/         toolchain.cmake, target.cmake, bsp.cpp, startup.c, flash.ld
    ├── arm-m0/         toolchain.cmake, target.cmake, bsp.cpp, startup.c, nrf51.ld
    ├── arm-m3-atfe/    toolchain.cmake, target.cmake, bsp.cpp (startup and syscalls come from ATfE)
    └── riscv32/        toolchain.cmake, target.cmake, bsp.cpp, startup.S, trap_entry.S, virt.ld
```

`toolchain.cmake` sets the compilers and flags (read before `project()`); `target.cmake` adds the target's sources and link options. To add a CPU, add a `targets/<name>/` folder with those two files and a `bsp.cpp`.

## Prerequisites

- CMake 3.16+ and Ninja
- QEMU: `qemu-system-arm` (Arm targets), `qemu-system-riscv32` (on Debian/Ubuntu in `qemu-system-misc`)
- The toolchain of each target you build:
  - `arm-m4`, `arm-m0`: [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads), `arm-none-eabi-gcc` on the PATH (Debian/Ubuntu: `gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib`). The test suite uses Arm's 15.2.rel1 on Windows and in the Docker image; Ubuntu 24.04's packages are GCC 13.2
  - `arm-m3-atfe`: [Arm Toolchain for Embedded](https://github.com/arm/arm-toolchain/releases) (`ATfE-<version>-<host>`), unpacked anywhere; pass `-DATFE_PATH=<path>` or set `ATFE_PATH`
  - `riscv32`: [xPack RISC-V Embedded GCC](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases), unpacked anywhere; pass `-DRISCV_TOOLCHAIN_PATH=<path>` or set `RISCV_TOOLCHAIN_PATH` (default `/opt/xpack-riscv-none-elf-gcc`)

`docker/Dockerfile` installs all of them, and `build_all.py --full` builds and runs every target (see "Full Test Suite" in `docs/BUILD.md`).

## Build and Run

```bash
cmake -B build-arm-m4 -G Ninja -DBM_TARGET=arm-m4
cmake --build build-arm-m4
qemu-system-arm -M mps2-an386 -cpu cortex-m4 -kernel build-arm-m4/delegate_app -nographic -semihosting-config enable=on,target=native

cmake -B build-arm-m0 -G Ninja -DBM_TARGET=arm-m0
cmake --build build-arm-m0
qemu-system-arm -M microbit -kernel build-arm-m0/delegate_app -nographic -semihosting-config enable=on,target=native

cmake -B build-arm-m3-atfe -G Ninja -DBM_TARGET=arm-m3-atfe -DATFE_PATH=/path/to/ATfE-22.1.0-Linux-x86_64
cmake --build build-arm-m3-atfe
qemu-system-arm -M mps2-an385 -cpu cortex-m3 -kernel build-arm-m3-atfe/delegate_app -nographic -semihosting-config enable=on,target=native

cmake -B build-riscv32 -G Ninja -DBM_TARGET=riscv32 -DRISCV_TOOLCHAIN_PATH=/path/to/xpack-riscv-none-elf-gcc-15.2.0-1
cmake --build build-riscv32
qemu-system-riscv32 -M virt -bios none -kernel build-riscv32/delegate_app -nographic -semihosting-config enable=on,target=native
```

QEMU exits by itself when the tests finish. Expected output (the target line and addresses vary):

```txt
=========================================
   BARE METAL DELEGATE SYSTEM ONLINE
   Cortex-M4, arm-none-eabi-gcc, QEMU mps2-an386
=========================================

[Test 1] Unicast Delegate (Free Function):
  [Callback] FreeFunction called! Value: 100
...
[Test 6] Timer Delegate (One-Shot):
  -> Starting Timer (200ms delay)...
  [Callback] Timer Expired!

=========================================
           ALL TESTS PASSED
=========================================
```

### Build Without CMake (Makefile)
The `Makefile` builds the `arm-m4` target the way a Makefile, Keil or IAR project would use DelegateMQ, with no CMake involved:

* DelegateMQ is a single source file, `src/DelegateMQ.cpp`.
* Options come from `config/DelegateMQConfig.h` (`DMQ_THREAD_NONE`, ...), picked up because `config/` is on the include path. No `-D` flags are needed.

```bash
make        # builds build-make/delegate_app
make run    # builds and runs it on QEMU
```

It needs GNU Make and a POSIX shell (Linux, macOS, or Git Bash/MSYS2 on Windows). See "Without CMake" in `docs/BUILD.md` for the general steps.

## Target Notes

### arm-m4
- `startup.c`: vector table (SysTick → `SysTick_Handler` in `bsp.cpp`), enables the FPU, copies `.data`, zeroes `.bss`, runs C++ static constructors (`__libc_init_array`), calls `main()`.
- `../common/retarget.c` (shared with `arm-m0`): newlib syscalls over semihosting: `_write` (`printf`), `_sbrk` (heap for DelegateMQ's allocations), `_exit` (semihosting `SYS_EXIT`), plus `_getentropy`, which newlib 4.4+ needs for libstdc++'s `std::random_device`.
- `flash.ld`: 4 MB flash at `0x00000000`, 4 MB RAM at `0x20000000`.

### arm-m0
The smallest Arm core, to catch what the larger ones forgive:
- **No atomic read-modify-write.** Armv6-M has no `LDREX`/`STREX`, so a `std::atomic` increment would compile to a libatomic call this toolchain can't link: the build itself is the check.
- **Unaligned access faults.** Every fault is a HardFault on Armv6-M; `startup.c`'s `HardFault_Handler` prints `HARD FAULT` and ends QEMU with status 1 instead of hanging.
- **16 KB RAM.** `nrf51.ld` (256 KB flash, 16 KB RAM) reserves 4 KB of stack and fails the link if `.data` + `.bss` + stack don't fit.
- **Timer:** the nRF51 has no SysTick, so `bsp.cpp` runs TIMER0 at 1 MHz (16 MHz, prescaler 4) with a compare every 1000 counts, cleared by hardware (`SHORTS`), interrupt IRQ 8 (`TIMER0_IRQHandler`), as a real nRF51 would.
- `startup.c`: vector table (core exceptions and the nRF51's 32 interrupts), copies `.data`, zeroes `.bss`, runs C++ static constructors, calls `main()`. No FPU to enable.

### arm-m3-atfe
Everything comes from ATfE's runtime, `armv7m_soft_nofp_exn_rtti_size` (selected by `--target=thumbv7m-unknown-none-eabi -mfpu=none -mno-unaligned-access`; it has the exceptions and RTTI DelegateMQ needs):
- `crt0-semihost.o`: vector table, reset handler, `.data`/`.bss` and C++ static constructor init. The vector table calls the weak `arm_systick_isr` for SysTick, which `bsp.cpp` defines.
- `libsemihost.a`: `printf`, `exit` and the rest over semihosting.
- `picolibcpp.ld`: linker script, laid out for `mps2-an385` with `--defsym` values in `target.cmake`.

| | `arm-m4` | `arm-m3-atfe` |
|---|---|---|
| Compiler / linker | GCC / GNU ld | clang / lld |
| C / C++ library | newlib / libstdc++ | picolibc / libc++ |
| Startup and syscalls | own `startup.c`, `retarget.c` | ATfE `crt0-semihost.o`, `libsemihost.a` |

### riscv32
- **Toolchain:** Ubuntu's `gcc-riscv64-unknown-elf` package is compiler-only, with no libc and no libstdc++, which can't link DelegateMQ (`Delegate::Equal()` calls `dynamic_cast`). The xPack toolchain bundles newlib and libstdc++/libsupc++. The target uses the `rv32imc/ilp32` multilib (`-march=rv32imc_zicsr -mabi=ilp32`): no atomics, soft float, a common baseline for single-core embedded RV32 parts, and (unlike `rv32imac`) one with a prebuilt libstdc++.
- **Startup:** the toolchain's generic `crt0.o` can't boot QEMU `virt` directly (`-bios none`): its `_start` reads argc/argv off a stack nothing has set up yet. `startup.S` provides `_start` instead (sets `sp`/`gp`, zeroes `.bss`, runs static constructors, calls `main()`), linked with `-nostartfiles`.
- **Timer:** the CLINT at `0x02000000` (`mtimecmp` for hart 0 at `+0x4000`, `mtime` at `+0xBFF8`), 10 MHz timebase, as QEMU's device tree reports (`qemu-system-riscv32 -M virt -machine dumpdtb=...` + `dtc`).
- **Traps:** RISC-V has one trap vector and saves nothing on entry, unlike Arm's NVIC. `trap_entry.S` saves and restores the caller-saved registers around `trap_dispatch()` in `bsp.cpp`, which reads `mcause` and handles the machine timer interrupt (`0x80000007`).
- **Exit:** `bsp.cpp` issues semihosting `SYS_EXIT` itself, because the toolchain's `libsemihost` `_exit` doesn't pass the status to QEMU.
- **`_getentropy`:** newlib's `libc.a` references it during startup; `bsp.cpp` stubs it (`ENOSYS`), as on a part with no hardware RNG.
- **Memory:** `virt.ld` puts everything in RAM at `0x80000000` (QEMU `virt` has no flash), defines `__global_pointer$` for gp-relative small data, and a 16 KB stack; the heap follows.

## Related Examples

- [bare-metal/keil](../keil/): Keil MDK (ARMCLANG) version, Cortex-M4
- [boards/stm32-freertos](../../boards/stm32-freertos/): FreeRTOS on a real STM32F4 board
