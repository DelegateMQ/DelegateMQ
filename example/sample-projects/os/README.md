# OS port samples

One sample per DelegateMQ OS port (`src/delegate-mq/port/os/<os>`), each checking delegates, signals, timers and thread policies on that OS. Most run the shared tests in [test/portable](../../../test/portable/), so every port is held to the same suite.

| Sample | OS port | Runs on |
| :--- | :--- | :--- |
| [freertos](./freertos/) | FreeRTOS (`DMQ_THREAD_FREERTOS`) | Linux (FreeRTOS POSIX simulator) |
| [freertos-bare-metal](./freertos-bare-metal/) | FreeRTOS (`DMQ_THREAD_FREERTOS`) | Windows (FreeRTOS Win32 simulator), Cortex-M3 under QEMU |
| [threadx](./threadx/) | ThreadX (`DMQ_THREAD_THREADX`) | Linux (ThreadX Linux/GNU simulation port) |
| [zephyr](./zephyr/) | Zephyr (`DMQ_THREAD_ZEPHYR`) | Linux (`native_sim`) |
| [cmsis-rtos2](./cmsis-rtos2/) | CMSIS-RTOS2 (`DMQ_THREAD_CMSIS_RTOS2`) | Linux (Zephyr `native_sim`), Cortex-M3 under QEMU |
| [nuttx](./nuttx/) | NuttX (`DMQ_THREAD_NUTTX`) | Linux (NuttX `sim` board) |
| [posix](./posix/) | POSIX threads (`DMQ_THREAD_POSIX`) | Linux |
| [qt](./qt/) | Qt (`DMQ_THREAD_QT`) | Linux with Qt 6 |
| [win32](./win32/) | Win32 threads (`DMQ_THREAD_WIN32`) | Windows |

The default desktop port, `DMQ_THREAD_STDLIB`, runs in every other desktop sample and the unit tests. Bare metal (`DMQ_THREAD_NONE`) is in [../bare-metal](../bare-metal/).

`build_all.py` builds and runs each one on the platform it supports; the test scripts name them `os/<sample>`.
