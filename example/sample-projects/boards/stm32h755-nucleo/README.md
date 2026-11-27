# NUCLEO-H755ZI-Q (STM32H755, dual core)

DelegateMQ on both cores of an STM32H755ZI: a Cortex-M7 at 400 MHz and a Cortex-M4 at 200 MHz, each with its own flash bank and RAM, and 64 KB of SRAM4 both can reach. This is the board for the dual-core topology in [CHOOSING.md](../../../../docs/CHOOSING.md#system-topology): the M7 on the network, the M4 on real-time work, and DelegateMQ between them over shared memory.

Status, step by step:

| Step | What | State |
| :--- | :--- | :--- |
| 1 | CMake project for both cores, flashing and console checks (`run.py`) | Builds; not yet run on the board |
| 2 | Cortex-M7: FreeRTOS and the DelegateMQ test suite | Builds; not yet run on the board |
| 3 | Cortex-M4: FreeRTOS and DelegateMQ | Next. For now it only completes the boot handshake and blinks LD2 |
| 4 | `ShmRingTransport` between the cores (SRAM4, HSEM interrupts) | Planned. The linker scripts already reserve the `.shared` section; the M7's MPU makes it non-cacheable |

## What runs

**Cortex-M7** (`cm7/`): sets the clocks and MPU, releases the M4, then runs the DelegateMQ FreeRTOS test suite of [os/freertos-bare-metal](../../os/freertos-bare-metal/README.md) (`main_delegate.cpp`). That is the same 15 tests that run on a Cortex-M3 under QEMU, now on real hardware: async delegates, signals, timers, `IsrDelegate` from a real timer interrupt (TIM6), `FullPolicy`, the start gate, the watchdog hook, and a check that nothing uses the heap once the system runs (`DMQ_ALLOCATOR` in static pools). Output goes to the ST-LINK's virtual COM port (USART3, 115200 8N1). When the suite ends, LD1 (green) stays on for a pass; LD3 (red) blinks for a failure.

**Cortex-M4** (`cm4/`): at reset it stops its domain and waits for the M7's release (hardware semaphore 0), then blinks LD2 (yellow) once a second.

## Requirements

- Arm GNU Toolchain (`arm-none-eabi-gcc` on the PATH), CMake 3.20+ and Ninja.
- STM32Cube FW_H7 package (HAL and CMSIS). Install it with STM32CubeMX (*Help > Manage embedded software packages*); it lands in `~/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1`, which the build finds by itself. Elsewhere, pass `-DSTM32CUBE_H7_DIR=<path>`.
- FreeRTOS from the example workspace (`external/FreeRTOS`, see [BUILD.md](../../../../docs/BUILD.md#example-ecosystem-sandbox)).
- To flash: `STM32_Programmer_CLI` from STM32CubeProgrammer, STM32CubeCLT or STM32CubeIDE. `run.py` finds it in their default folders, or set `STM32_PROGRAMMER_CLI`. It also needs pyserial.

## Build and run

```
cmake -B build -G Ninja
cmake --build build
python run.py
```

`run.py` flashes `build/h755_cm7.elf` (bank 1, `0x08000000`) and `build/h755_cm4.elf` (bank 2, `0x08100000`), resets the board, prints the console and returns the suite's status (0 = all tests passed). `--no-flash` only resets and reads; `--port` names the console if the ST-LINK's port isn't found by its USB ID.

## Board notes

- **Power supply: SMPS.** This board is wired for the STM32H755's internal SMPS. Firmware that sets up the LDO instead (the default in many generic H7 examples) leaves the chip unreachable by the debugger until a recovery: BOOT0 jumper to 3.3 V, connect under reset, mass erase. The build defines `USE_PWR_DIRECT_SMPS_SUPPLY`, which the startup code applies before anything else; keep it.
- **Both cores boot** (option bytes `BCM7` and `BCM4` set, as shipped). `run.py` checks this before flashing.
- **Memory:** M7 code in flash bank 1, data in AXI SRAM (512 KB at `0x24000000`); M4 code in bank 2, data in D2 SRAM (288 KB at `0x10000000`); SRAM4 (64 KB at `0x38000000`) is the `.shared` section in both linker scripts.
- **Cache:** the M7 has a data cache and the M4 does not, so the M7's MPU marks SRAM4 non-cacheable. Otherwise one core could read stale data the other already changed.
- **FreeRTOS port:** `GCC/ARM_CM4F` on both cores. The `ARM_CM7/r0p1` port is only for r0p1 silicon, and the H7's Cortex-M7 is a later revision.

Clock setup, MPU background region and the boot handshake follow ST's `BootCM4_CM7` template for this board family (STM32CubeH7, `Projects/NUCLEO-H745ZI-Q/Templates`; the H745 projects apply to the H755). The linker scripts are ST's H755 scripts with the changes noted at the top of each.
