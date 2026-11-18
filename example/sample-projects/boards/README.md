# Board samples

DelegateMQ on real hardware. These need the board and its vendor tools, so `build_all.py` doesn't build them.

| Sample | Board | Shows |
| :--- | :--- | :--- |
| [stm32-freertos](./stm32-freertos/) | STM32F4 Discovery (STM32F407VG) | FreeRTOS on the board (STM32CubeIDE project) and a PC client talking to it over a UART |

Hardware-free versions of the same ideas run under QEMU in [../bare-metal](../bare-metal/) and [../os](../os/).
