# DelegateMQ Bare-Metal ARM Example

This project demonstrates how to build and run the **DelegateMQ** C++ delegate library on a **Bare-Metal ARM Cortex-M4** target.

It serves as a proof-of-concept for using modern C++ features (Delegates, Signals, Lambdas, RAII) in a constrained embedded environment with **no operating system (RTOS)** and **no threading support**.

## `Timer` on a real hardware interrupt (SysTick)

Test 6 wires up a genuine Cortex-M4 SysTick interrupt (1ms period, configured directly via the SCS registers -- `SYST_CSR`/`SYST_RVR`/`SYST_CVR`, no CMSIS-Core) that both increments `g_ticks` (driving `dmq::os::BareMetalClock`) and calls `dmq::util::Timer::ProcessTimers()` directly from ISR context. This is the first time `dmq::os::BareMetalCriticalSection` (the ISR-safe lock `Timer::GetLock()` uses) has actually been exercised from a real interrupt on this port rather than just reasoned through -- see `CLAUDE.md`'s "ISR-Safe Locking" section.

Getting this working surfaced two real, previously-undetected issues, both now fixed:

* **`Timer.h`/`Timer.cpp` were unreachable under `DMQ_THREAD_NONE`** -- `DelegateMQ.h` excluded the `#include "extras/util/Timer.h"` for any bare-metal build, and `Port.cmake`'s bare-metal `UTIL_SOURCES` glob explicitly excluded `Timer.cpp`, both under the stale assumption that `Timer` needs real OS mutexes. It doesn't: `Timer::GetLock()` uses `dmq::CriticalSection`, and `BareMetalCriticalSection.h`/`BareMetalClock.h` exist specifically to make `Timer` usable with no RTOS at all (their own doc comments say so). Both are fixed to include `Timer` for `DMQ_THREAD_NONE`, while `AsyncInvoke.h`/`TimerDelegate.h`/`ThreadMonitor.cpp` — which do need a real `dmq::IThread` — stay excluded.
* **This sample's own `CMakeLists.txt` never linked any `extras/`/`port/` `.cpp` file at all** — it globbed `DMQ_PORT_SOURCES` into an unused `SOURCES` variable and never passed it (or `DMQ_EXTRAS_SOURCES`) to `add_executable()`. Fixed by adding `${DMQ_EXTRAS_SOURCES}` specifically (not the full `${DMQ_PORT_SOURCES}`, which also includes `port/fault/Fault.cpp` — that would collide at link time with this file's own `FaultHandler()`/`WatchdogHandler()` definitions).

## Prerequisites

To build and run this project, you need the following tools installed on your path:

1.  **ARM GCC Toolchain:** [Arm GNU Toolchain Downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
    * Ensure `arm-none-eabi-gcc` is in your system PATH.
2.  **CMake:** [Download CMake](https://cmake.org/download/)
3.  **Ninja Build System:** [Download Ninja](https://github.com/ninja-build/ninja/releases)
    * Recommended for faster builds, though Makefiles work as well.
4.  **QEMU (for ARM):** [Download QEMU](https://www.qemu.org/download/)
    * Required to simulate the hardware and see the output.

## Build Instructions

Open a terminal in the project root directory and run the following commands:

### Configure
Generate the build files using CMake and Ninja.

```bash
cmake -B build -G "Ninja"
```

### Build
Compile the application.

`cmake --build build`

This will produce an executable ELF file at build/delegate_app.

## Run Instructions

This application is configured for the ARM MPS2-AN386 board (Cortex-M4). It uses Semihosting to pipe printf output directly from the emulated CPU to your console.

Run the application using QEMU:

```bash
qemu-system-arm -M mps2-an386 -cpu cortex-m4 -kernel build/delegate_app -nographic -semihosting-config enable=on,target=native
```

**Expected Output**

You should see the bare-metal system boot and run a series of delegate tests:

```txt
--- ENTERING RESET HANDLER ---
--- INITIALIZING C++ ---
--- JUMPING TO MAIN ---

=========================================
   BARE METAL DELEGATE SYSTEM ONLINE     
=========================================

[Test 1] Unicast Delegate (Free Function):
  [Callback] FreeFunction called! Value: 100
...
[Test 5] Signals & Scoped Connections:
  -> Creating ScopedConnection inside block...
  -> Firing Signal (Expect Callback):
  [Callback] FreeFunction called! Value: 500
...
[Test 6] Timer Delegate (One-Shot):
  -> Starting Timer (200ms delay)...
  [Callback] Timer Expired!

=========================================
           ALL TESTS PASSED              
=========================================
To Exit QEMU: Press Ctrl+a, release, then press x.
```

## Technical Details

Target Hardware: ARM Cortex-M4 (Floating Point Unit enabled).

Linker Script: Custom flash.ld defining 4MB Flash and 4MB RAM.

Startup Code: `startup.c` handles vector tables, data copying, BSS zeroing, and C++ static constructor initialization (`__libc_init_array`).

Retargeting: `retarget.c` intercepts standard I/O (`_write`, `_sbrk`) to enable printf via Semihosting and memory allocation for C++ delegates.