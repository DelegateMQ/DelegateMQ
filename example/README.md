# DelegateMQ Examples

| Directory | Purpose |
| :--- | :--- |
| **[sample-code](./sample-code/README.md)** | Short, single-file examples of common patterns built with DelegateMQ: active object, async callbacks and futures, signal/slot, observer, producer/consumer, reactor/proactor, scatter-gather, DataBus, remote communication, and more. Not standalone projects: they are compiled into the `test` build and run from `delegate_app`. |
| **[sample-projects](./sample-projects/README.md)** | Standalone CMake projects, each focused on one platform, RTOS port, or transport/serializer pairing: bare metal, FreeRTOS, ThreadX, Zephyr, NuttX, ZeroMQ, NNG, MQTT, UDP/TCP, serial, DataBus. See its [README](./sample-projects/README.md) for the full list. |
| **[sample-interop](./sample-interop/README.md)** | Cross-language demo: a C++ server talking to C# and Python clients through the DelegateMQ interop DLL. |
| **[cellutron](./cellutron/CELLUTRON.md)** | Multi-node reference application: a simulated safety-critical cell processing instrument with GUI, controller and safety CPUs on FreeRTOS/ThreadX simulators, linked by a distributed DataBus. See [CELLUTRON.md](./cellutron/CELLUTRON.md). |
| **[pumptron](./pumptron/PUMPTRON.md)** | Two-CPU hardware reference application: a pump controller on an STM32F4 Discovery board (FreeRTOS), monitored and commanded from an FTXUI desktop console over RS-232 using the DataBus. Also runs fully on the PC via the FreeRTOS simulator. See [PUMPTRON.md](./pumptron/PUMPTRON.md). |
| **[sensor-node](./sensor-node/README.md)** | A sensor node for the ESP32-S3 (dual core, Wi-Fi) and its PC client: sampling, control and networking on separate threads that talk only through DelegateMQ, a touch interrupt through `IsrDelegate`, and DataBus over UDP to the PC. Built and tested on Windows and Linux first, with stand-ins for the hardware; the board port is in progress. |
