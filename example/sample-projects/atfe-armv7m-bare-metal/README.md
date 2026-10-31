# DelegateMQ ATfE Armv7-M Bare-Metal Example

This project demonstrates the **DelegateMQ** C++ delegate library on a **bare-metal Armv7-M (Cortex-M3)** target using the **Arm Toolchain for Embedded (ATfE)** — Arm's official Clang/LLVM-based embedded toolchain.

It serves as a proof-of-concept for using synchronous delegates, multicast delegates, and signals with **no RTOS** and **no threading support**, built entirely with an open-source, freely downloadable toolchain.

## Prerequisites

1. **Arm Toolchain for Embedded (ATfE)**
   - Download: [github.com/ARM-software/toolchains](https://github.com/ARM-software/toolchains)
   - Download `ATfE-22.1.0-Windows-x86_64.zip` and extract to `C:\ATfE-22.1.0-Windows-x86_64`
   - Linux: download `ATfE-22.1.0-Linux-x86_64.tar.xz` and extract it anywhere
   - If installed to a different path (or on Linux), pass `-DATFE_PATH=<path>` to CMake

2. **CMake** 3.16 or later

3. **Ninja** build system (recommended)

4. **QEMU** for ARM
   - Download: [qemu.org/download](https://www.qemu.org/download/)
   - Ensure `qemu-system-arm` is on your PATH

## Key Differences from `bare-metal-arm`

| | `bare-metal-arm` | `atfe-armv7m-bare-metal` |
|---|---|---|
| Compiler | `arm-none-eabi-gcc` (GCC) | ATfE `clang` (LLVM/Clang) |
| C library | `newlib` (`--specs=nano.specs`) | `picolibc` (bundled in ATfE) |
| C++ library | `libstdc++` | `libc++` |
| Linker | GNU ld | `lld` (LLVM) |
| Startup | Custom `startup.c` | ATfE `crt0-semihost.o` (built-in) |
| Semihosting | Custom `retarget.c` | ATfE `libsemihost.a` (built-in) |
| Target CPU | Cortex-M4 (Armv7E-M) | Cortex-M3 (Armv7-M) |
| QEMU machine | `mps2-an386` | `mps2-an385` |

## Runtime Variant

ATfE provides prebuilt runtime libraries for many Arm configurations. This project uses:

```
armv7m_soft_nofp_exn_rtti_size
```

Selected by the compiler flags `--target=thumbv7m-unknown-none-eabi -mfpu=none -mno-unaligned-access`. This variant includes exceptions and RTTI support (required by DelegateMQ) and is optimized for size.

## Build Instructions

```bash
# Configure
cmake -B build -G "Ninja" -DATFE_PATH="C:/ATfE-22.1.0-Windows-x86_64"
# Linux: cmake -B build -G "Ninja" -DATFE_PATH=/path/to/ATfE-22.1.0-Linux-x86_64

# Build
cmake --build build
```

The output ELF is at `build/delegate_app`.

## Run Instructions

Simulate with QEMU on the MPS2-AN385 board (Cortex-M3):

```bash
qemu-system-arm -M mps2-an385 -cpu cortex-m3 -kernel build/delegate_app -nographic -semihosting-config enable=on,target=native
```

**Expected output:**

```
=========================================
   BARE METAL DELEGATE SYSTEM ONLINE
=========================================

[Test 1] Unicast Delegate (Free Function):
  [Callback] FreeFunction called! Value: 100

[Test 2] Unicast Delegate (Lambda):
  [Callback] Lambda called! Capture: 42, Arg: 200

[Test 3] Multicast Delegate (Broadcast):
Firing all 3 targets...
  [Callback] FreeFunction called! Value: 300
  [Callback] MemberFunc called! Value: 300 ...
  [Callback] Multicast Lambda called! Val: 300

[Test 4] Removing a Delegate:
Firing remaining targets (Expected: 2)...
  [Callback] MemberFunc called! Value: 400 ...
  [Callback] Multicast Lambda called! Val: 400

[Test 5] Signals & Scoped Connections:
  -> Creating ScopedConnection inside block...
  -> Firing Signal (Expect Callback):
  [Callback] FreeFunction called! Value: 500
  -> Exiting block (ScopedConnection will destruct)...
  -> Firing Signal outside block (Expect NO Callback):

=========================================
           ALL TESTS PASSED
=========================================
To Exit QEMU: Press Ctrl+a, release, then press x.
```

## Technical Details

| Setting | Value |
|---------|-------|
| Target triple | `thumbv7m-unknown-none-eabi` |
| CPU | Cortex-M3 (Armv7-M) |
| Float ABI | Soft (no FPU) |
| C++ Standard | C++17 |
| Flash | 4 MB @ 0x00000000 |
| RAM | 4 MB @ 0x20000000 |
| Linker script | `picolibcpp.ld` (from ATfE runtime) |
| Startup | `crt0-semihost.o` (handles vector table, data/BSS init, C++ ctors) |

## Related Examples

- [bare-metal-arm](../bare-metal-arm/) — same concept built with ARM GCC + newlib
- [keil-bare-metal](../keil-bare-metal/) — Keil MDK (ARMCLANG) version, Cortex-M4
