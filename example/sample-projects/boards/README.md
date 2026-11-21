# Board samples

DelegateMQ on real hardware. These need the board and its vendor tools, so `build_all.py` doesn't build them.

| Sample | Board | Shows |
| :--- | :--- | :--- |
| [stm32-freertos](./stm32-freertos/README.md) | STM32F4 Discovery (STM32F407VG) | FreeRTOS on the board (STM32CubeIDE project) and a PC client talking to it over a UART |
| [esp32s3-devkitc](./esp32s3-devkitc/README.md) | ESP32-S3-DevKitC-1 N8R8 (dual-core Xtensa LX7, 8 MB PSRAM) | ESP-IDF's FreeRTOS on both cores: a worker thread owning the RGB LED, a `Timer`, and the BOOT button's interrupt through an `IsrDelegate` (PlatformIO or `idf.py`) |

Hardware-free versions of the same ideas run under QEMU in [../bare-metal](../bare-metal/README.md) and [../os](../os/README.md).
