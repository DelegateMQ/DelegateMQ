# Bare-metal samples

DelegateMQ with no OS (`DMQ_THREAD_NONE`): synchronous delegates, signals and timers, with `Timer::ProcessTimers()` called from a hardware timer interrupt.

| Sample | Shows | Toolchain |
| :--- | :--- | :--- |
| [qemu](./qemu/) | One set of self-checking tests built for Cortex-M4, Cortex-M0, Cortex-M3 and RV32IMC, each run under QEMU. `build_all.py` builds and runs it. | ARM GCC, ATfE Clang, xPack RISC-V GCC |
| [keil](./keil/) | The same idea as a Keil MDK project for a Cortex-M4. Not built by `build_all.py`. | Keil MDK (ARMCLANG) |

FreeRTOS on a Cortex-M3 under QEMU is in [../os/freertos-bare-metal](../os/freertos-bare-metal/), and real hardware in [../boards](../boards/).
