# Board samples

DelegateMQ on real hardware. These need the board and its vendor tools, so `build_all.py` doesn't run them. It builds `stm32-freertos` (firmware and PC client) when the ARM toolchain and STM32Cube FW_F4 package are present.

| Sample | Board | Shows |
| :--- | :--- | :--- |
| [stm32-freertos](./stm32-freertos/README.md) | STM32F4 Discovery (STM32F407VG) | FreeRTOS on the board (STM32CubeIDE project, or CMake) and a PC client talking to it over a UART. `build_all.py` builds both |
| [stm32h755-nucleo](./stm32h755-nucleo/README.md) | NUCLEO-H755ZI-Q (STM32H755: Cortex-M7 and Cortex-M4) | Both cores from one CMake build. The Cortex-M7 runs the FreeRTOS test suite of `os/freertos-bare-metal` on real hardware; the Cortex-M4 boots on its release. Shared memory between the cores comes next. Not yet run on the board |
| [esp32s3-devkitc](./esp32s3-devkitc/README.md) | ESP32-S3-DevKitC-1 N8R8 (dual-core Xtensa LX7, 8 MB PSRAM) | ESP-IDF's FreeRTOS on both cores: a worker thread owning the RGB LED, a `Timer`, and the BOOT button's interrupt through an `IsrDelegate` (PlatformIO or `idf.py`) |

Hardware-free versions of the same ideas run under QEMU in [../bare-metal](../bare-metal/README.md) and [../os](../os/README.md).
