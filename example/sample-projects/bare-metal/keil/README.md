# DelegateMQ Keil MDK Bare-Metal Example

This project demonstrates the **DelegateMQ** C++ delegate library on a **bare-metal ARM Cortex-M4** target using the **Keil MDK** toolchain (ARMCLANG).

It serves as a proof-of-concept for using synchronous delegates, multicast delegates, and signals in a constrained embedded environment with **no RTOS** and **no threading support**.

## Prerequisites

- **Keil MDK** with ARMCLANG v6.x compiler
- **ARM.CMSIS** pack v5.9.0 or later (install via Pack Installer)

## Project Setup

### Include Path

The project requires the DelegateMQ source directory on the compiler include path. In the Keil project options under **C/C++ > Include Paths**, the path is set to:

```
.\..\..\..\..\src\delegate-mq
```

This resolves to `DelegateMQ/src/delegate-mq` relative to the repository root.

### Preprocessor Defines

No additional defines are required for bare-metal synchronous-only use. DelegateMQ automatically selects `DMQ_THREAD_NONE` when no thread implementation is specified.

### Files

| File | Description |
|------|-------------|
| `DelegateMQ.uvprojx` | Keil MDK project file |
| `DelegateMQ.uvoptx` | Target and debugger options |
| `main.cpp` | Application entry point with delegate examples |
| `startup.s` | Cortex-M4 vector table and reset handler |

## What the Example Demonstrates

`main.cpp` exercises the core DelegateMQ delegate types without any OS or thread dependency:

- **`MakeDelegate`** — binds free functions, member functions, and static functions for synchronous invocation
- **`UnicastDelegate`** — stores a single delegate, invokes on `operator()`
- **`MulticastDelegate`** — stores multiple delegates, broadcasts to all on `operator()`
- **`Signal`** — thread-safe signal/slot with RAII `ScopedConnection`; connection auto-disconnects when the handle goes out of scope

## Target Configuration

| Setting | Value |
|---------|-------|
| Device | ARMCM4 (ARM Cortex-M4) |
| Compiler | ARMCLANG v6.21 |
| C++ Standard | C++17 |
| Flash | 0x00000000, 256 KB |
| RAM | 0x20000000, 128 KB |

## Related Examples

- [bare-metal/qemu](../qemu/) — same concept built with CMake + ARM GCC (or ATfE, or RISC-V GCC) + QEMU
- [os/freertos-bare-metal](../../os/freertos-bare-metal/) — adds FreeRTOS thread support for async delegates
