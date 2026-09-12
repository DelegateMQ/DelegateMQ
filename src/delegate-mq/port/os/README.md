# OS / Threading Layer

This directory contains the Operating System abstraction layer for **DelegateMQ**. It provides the concrete threading implementations required to execute asynchronous delegates.

By abstracting the threading model behind the `dmq::IThread` interface, DelegateMQ can run seamlessly on everything from high-performance servers (Windows/Linux) to resource-constrained embedded systems (FreeRTOS, ThreadX, Zephyr).

## Implementations

The subdirectories contain the platform-specific thread wrappers:

* **`stdlib`**: Standard C++11 implementation.
    * *Target:* Windows, Linux, macOS, or any OS with a compliant C++ Standard Library.
    * *Implementation:* Uses `std::thread`, `std::mutex`, `std::condition_variable`, and `std::promise`.
* **`freertos`**: Real-Time OS implementation.
    * *Target:* Embedded ARM Cortex-M (STM32, NXP, etc.), ESP32, and others running FreeRTOS.
    * *Implementation:* Uses native FreeRTOS primitives: `xTaskCreate`, `xQueueSend/Receive`, and `vTaskDelay`.
* **`threadx`**: Azure RTOS (ThreadX) implementation.
    * *Target:* Enterprise embedded devices running Eclipse ThreadX (formerly Azure RTOS).
    * *Implementation:* Uses `tx_thread_create` and `tx_queue_send/receive`.
* **`zephyr`**: Zephyr RTOS implementation.
    * *Target:* Modern IoT devices supported by the Zephyr Project.
    * *Implementation:* Uses kernel primitives `k_thread_create` and `k_msgq_put/get`.
* **`cmsis-rtos2`**: CMSIS-RTOS API v2 implementation.
    * *Target:* Any kernel compliant with the ARM CMSIS-RTOS2 standard (Keil RTX5, Micrium OS, etc.).
    * *Implementation:* Uses standard APIs `osThreadNew` and `osMessageQueuePut/Get`.
* **`nuttx`**: Apache NuttX RTOS implementation.
    * *Target:* Any board supported by NuttX (POSIX-compliant embedded RTOS), including its `sim` simulation target.
    * *Implementation:* Uses NuttX's native POSIX APIs directly: `pthread_create`, `pthread_mutex_t`, `sem_t`, and a POSIX `mqueue` (with native `msg_prio`-based priority) for message passing.
* **`qt`**: Qt Framework implementation.
    * *Target:* Desktop or embedded GUI applications using Qt.
    * *Implementation:* Uses `QThread` and the native Signal & Slot mechanism (`moveToThread`) to safely dispatch delegates to the Qt Event Loop.

## Configuration

To select the appropriate threading model, set the `DMQ_THREAD` variable in your CMake configuration:

```cmake
# Options:
# DMQ_THREAD_STDLIB        (Default for PC/Linux)
# DMQ_THREAD_FREERTOS      (FreeRTOS)
# DMQ_THREAD_THREADX       (Azure RTOS ThreadX)
# DMQ_THREAD_ZEPHYR        (Zephyr RTOS)
# DMQ_THREAD_CMSIS_RTOS2   (ARM CMSIS-RTOS2)
# DMQ_THREAD_NUTTX         (Apache NuttX RTOS)
# DMQ_THREAD_QT            (Qt Framework)
# DMQ_THREAD_NONE          (For Bare-metal super-loops)

set(DMQ_THREAD "DMQ_THREAD_STDLIB" CACHE STRING "" FORCE)
```

## Custom Porting

If you need to run DelegateMQ on a different OS (e.g., VxWorks, QNX, or a proprietary kernel), you can simply implement the `dmq::IThread` interface and inject your custom thread wrapper into the library.
