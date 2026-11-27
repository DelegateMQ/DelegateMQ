# Sample Projects

The samples are grouped by what they show. Each group folder has a README listing its samples, and each sample has its own README and build.

| Group | Samples of |
| :--- | :--- |
| [os/](./os/README.md) | One sample per OS port (`dmq::IThread`), each running the shared portable tests |
| [bare-metal/](./bare-metal/README.md) | No OS (`DMQ_THREAD_NONE`): QEMU targets and a Keil project |
| [boards/](./boards/README.md) | Real hardware |
| [transport/](./transport/README.md) | Remote delegates over a transport and serializer |
| [databus/](./databus/README.md) | `dmq::databus::DataBus` publish/subscribe |
| [system-architecture/](./system-architecture/README.md) | One client/server system, built with and without external libraries, plus a Python client |
| [integration/](./integration/README.md) | Bringing DelegateMQ into your own build |
| [patterns/](./patterns/README.md) | Application designs, such as an active-object state machine and a library on its own thread |

The test scripts (`scripts/build_all.py`) name a sample by its folder, e.g. `os/threadx` or `databus/relay`.

## os/

Each sample checks DelegateMQ's delegate types (sync, async, async-wait, multicast, signal), timers and thread policies on one OS port, mostly with the shared tests in [test/portable](../../test/portable/PortableTests.h).

| Sample | Description | Threading (`dmq::IThread`) | Platform / Toolchain |
| :--- | :--- | :--- | :--- |
| **[os/freertos](./os/freertos/README.md)** | FreeRTOS on the official POSIX/Linux simulation port (each task is a real pthread). Linux only. | FreeRTOS | Linux GCC |
| **[os/freertos-esp32](./os/freertos-esp32/README.md)** | FreeRTOS on an ESP32 (dual-core Xtensa) under ESP-IDF, run under Espressif's QEMU: SMP, and a hardware timer interrupt. Needs ESP-IDF; `build_all.py` builds it on Linux when `IDF_PATH` is set (the Docker image has it). | FreeRTOS (ESP-IDF) | ESP-IDF + QEMU |
| **[os/freertos-bare-metal](./os/freertos-bare-metal/README.md)** | FreeRTOS on the Windows simulator (32-bit build), and on a Cortex-M3 under QEMU (`arm-m3` target). | FreeRTOS | MSVC (Win32), ARM GCC + QEMU |
| **[os/threadx](./os/threadx/README.md)** | ThreadX on the official Linux/GNU simulation port. Linux only. | ThreadX | Linux GCC |
| **[os/zephyr](./os/zephyr/README.md)** | Zephyr on the official `native_sim` port (host GCC, no cross-compiler) and on a Cortex-M3 under QEMU (`mps2/an385`). Needs a Zephyr workspace; `build_all.py` builds it on Linux when `ZEPHYR_BASE` is set (the Docker image has one). Linux only. | Zephyr | Linux GCC + Zephyr |
| **[os/cmsis-rtos2](./os/cmsis-rtos2/README.md)** | CMSIS-RTOS2 on Zephyr's CMSIS-RTOS v2 layer, on `native_sim` and on a Cortex-M3 under QEMU (`mps2/an385`). Needs a Zephyr workspace. Linux only. | CMSIS-RTOS2 | Linux GCC + Zephyr |
| **[os/nuttx](./os/nuttx/README.md)** | NuttX on its own `sim` board (a host process). Needs NuttX and nuttx-apps checkouts; `build_all.py` builds it on Linux when `NUTTX_SRC` is set (the Docker image has them). Linux only. | NuttX | Linux GCC + NuttX build tools |
| **[os/posix](./os/posix/README.md)** | The `DMQ_THREAD_POSIX` port (`dmq::os::PosixThread`), built on `pthread_create`/`pthread_mutex_t`/`pthread_cond_t` instead of `std::thread`. Linux only. | POSIX pthreads | Linux GCC |
| **[os/qt](./os/qt/README.md)** | The `DMQ_THREAD_QT` port (`dmq::os::QtThread`): each thread is a `QThread` with its own event loop. Needs Qt 6 Core; built in CI and by `build_all.py` on Linux when Qt is installed (the Docker image has it). | Qt 6 | Linux GCC + Qt 6 |
| **[os/win32](./os/win32/README.md)** | The `DMQ_THREAD_WIN32` port (`dmq::os::Win32Thread`), built on `CreateThread`/`CRITICAL_SECTION`/`CONDITION_VARIABLE` instead of `std::thread`. Same tests as `os/posix`. Windows only. | Win32 threads | MSVC |

## bare-metal/

| Sample | Description | Threading (`dmq::IThread`) | Platform / Toolchain |
| :--- | :--- | :--- | :--- |
| **[bare-metal/qemu](./bare-metal/qemu/README.md)** | No RTOS: one set of self-checking tests (delegates, signals, a timer fired by a real hardware interrupt) built for several targets, all run on QEMU: Cortex-M4 and Cortex-M0 (ARM GCC, newlib), Cortex-M3 (ATfE clang, picolibc), RV32IMC (xPack RISC-V GCC). | None | ARM GCC, ATfE Clang, xPack RISC-V GCC |
| **[bare-metal/keil](./bare-metal/keil/README.md)** | Bare-metal example for ARM Cortex-M4. | None | Keil MDK (ARMCLANG) |

## boards/

| Sample | Description | Threading (`dmq::IThread`) | Platform / Toolchain |
| :--- | :--- | :--- | :--- |
| **[boards/stm32-freertos](./boards/stm32-freertos/README.md)** | FreeRTOS on an STM32F4 Discovery board, talking to a PC client over a UART. | FreeRTOS | STM32CubeIDE / ARM GCC |
| **[boards/stm32h755-nucleo](./boards/stm32h755-nucleo/README.md)** | Both cores of a NUCLEO-H755ZI-Q (STM32H755, Cortex-M7 and Cortex-M4) from one CMake build: the Cortex-M7 runs the FreeRTOS test suite of `os/freertos-bare-metal` on real hardware, the Cortex-M4 boots on its release. Shared memory between the cores comes next. | FreeRTOS | ARM GCC, CMake, STM32Cube FW_H7 |
| **[boards/esp32s3-devkitc](./boards/esp32s3-devkitc/README.md)** | ESP-IDF's FreeRTOS on both cores of an ESP32-S3-DevKitC-1 (N8R8): a worker thread owning the RGB LED, a `Timer`, and the BOOT button's interrupt through an `IsrDelegate`. A starting point. | FreeRTOS (ESP-IDF) | PlatformIO or ESP-IDF 5.4.1 |

## transport/

Remote delegates invoke a target function that runs in another processor or process. Transport and serializer are independent: any transport works with any serializer.

| Sample | Description | Threading (`dmq::IThread`) | Serialization (`dmq::ISerializer`) | Transport (`dmq::IDispatcher`) |
| :--- | :--- | :--- | :--- | :--- |
| **[transport/serializer](./transport/serializer/README.md)** | Remote delegate sender/receiver app, built for the transport and serializer you choose (`-DSAMPLE_TRANSPORT`, `-DSAMPLE_SERIALIZER`): UDP or TCP sockets (with acknowledgments and retries), a Windows named pipe or a Linux Unix domain socket with no third-party library; UDP over the lwIP stack (Linux), or ZeroMQ or NNG; the built-in serializer, Bitsery, Cereal, MessagePack or RapidJSON. | `std::thread` | built-in, Bitsery, Cereal, MessagePack, RapidJSON | UDP, TCP, Windows pipe, Unix domain socket, lwIP UDP, ZeroMQ, NNG |
| **[transport/in-process](./transport/in-process/main.cpp)** | The smallest remote delegate: sender and receiver in one process, the "transport" a shared `std::stringstream`. Windows or Linux. | `std::thread` | `operator<<` / `operator>>` | `std::stringstream` |
| **[transport/shm-ring](./transport/shm-ring/README.md)** | Remote delegates between two processes over shared memory (`Win32ShmTransport` / `LinuxShmTransport` over `ShmRingTransport`): the desktop version of two cores on an AMP MCU. Side A starts side B itself. Windows or Linux. | `std::thread` | `raw::Serializer` | shared memory (file mapping / `shm_open`) |
| **[transport/serialport](./transport/serialport/README.md)** | Remote delegate over a serial port, using libserialport. | `std::thread` | `dmq::Serializer` class | `libserialport` |
| **[transport/mqtt](./transport/mqtt/README.md)** | Remote delegate over MQTT with RapidJSON (publisher and subscriber apps, and a broker). | `std::thread` | RapidJSON | MQTT |
| **[transport/zephyr-udp](./transport/zephyr-udp/README.md)** | Remote delegate over Zephyr's native UDP sockets (BSD Socket API) on `native_sim`, loopback only. Needs a Zephyr workspace. Linux only. | Zephyr | `dmq::Serializer` class | Zephyr UDP Socket |
| **[transport/netx-udp](./transport/netx-udp/README.md)** | The `transport/serializer` app over NetX Duo UDP (`NetXUdpTransport`) on ThreadX's Linux simulation port, through NetX's loopback interface. Needs ThreadX and NetX Duo (fetched by `01_fetch_repos.py`). Linux only. | ThreadX | `dmq::Serializer` class | NetX Duo UDP |

## databus/

See [databus/README.md](./databus/README.md).

| Sample | Description | Threading (`dmq::IThread`) | Serialization (`dmq::ISerializer`) | Transport (`dmq::IDispatcher`) |
| :--- | :--- | :--- | :--- | :--- |
| **[databus/local](./databus/local/README.md)** | Single-process `dmq::databus::DataBus` across three threads (LVC/lifespan, sync vs. async, rate limiting, filtering, deadline monitoring, `dmq-spy`). | `std::thread` | None between threads (`SpyBridge` to `dmq-spy`: built-in) | None between threads (`SpyBridge` to `dmq-spy`: UDP, optional) |
| **[databus/basic](./databus/basic/README.md)** | Distributed sensor/actuator system using `dmq::databus::DataBus` over UDP. | `std::thread` | `dmq::Serializer` class | UDP Socket |
| **[databus/multicast](./databus/multicast/README.md)** | One-to-many distribution over UDP multicast. | `std::thread` | `dmq::Serializer` class | UDP Multicast |
| **[databus/relay](./databus/relay/README.md)** | Three nodes across two different links: sensor ↔ gateway over UDP, gateway ↔ monitor over local IPC. The gateway relays topics both ways with `AddRelayTopic` and uses two transports in one process. Windows: named pipe / Linux: Unix domain socket. | `std::thread` | `dmq::Serializer` class | UDP Socket + Win32 Pipe / Unix Domain Socket |
| **[databus/freertos](./databus/freertos/README.md)** | FreeRTOS server (Win32 simulator, 32-bit) publishing sensor data to a Linux/Windows client over UDP, with the client sending rate-control commands back. | FreeRTOS | `dmq::Serializer` class | UDP Socket |
| **[databus/zephyr](./databus/zephyr/README.md)** | Zephyr server (`native_sim`, offloaded sockets over the host loopback) and a Linux/Windows client over UDP, in both directions. The server needs a Zephyr workspace. | Zephyr | `dmq::Serializer` class | UDP Socket |
| **[databus/interop](./databus/interop/README.md)** | C++ server with Python and C# clients, using MessagePack. | `std::thread` | MessagePack | UDP Socket |
| **[databus/mqtt-gateway](./databus/mqtt-gateway/README.md)** | Simulated thermostat exposed to MQTT tools as per-topic JSON via `JsonTopics` and `MqttBridge`; accepts setpoint commands. | `std::thread` | JSON (`JsonTopics`) | MQTT (Paho) |
| **[databus/shapes](./databus/shapes/README.md)** | Terminal shapes demo over UDP multicast, using FTXUI. | `std::thread` | `dmq::Serializer` class | UDP Multicast |

## system-architecture/

| Sample | Description | Threading (`dmq::IThread`) | Serialization (`dmq::ISerializer`) | Transport (`dmq::IDispatcher`) |
| :--- | :--- | :--- | :--- | :--- |
| **[system-architecture/full](./system-architecture/full/README.md)** | Client/server system with managers, alarms and point-to-point RPC (`RemoteDispatcher`), using external libraries. | `std::thread` | MessagePack | ZeroMQ |
| **[system-architecture/no-deps](./system-architecture/no-deps/README.md)** | The same system with no external library, over UDP. Windows or Linux. | `std::thread` | `operator<<` / `operator>>` | UDP Socket |
| **[system-architecture/python](./system-architecture/python/README.md)** | A Python client for `system-architecture/full`'s server. | `std::thread` | MessagePack | ZeroMQ |

## integration/

| Sample | Description | Threading (`dmq::IThread`) | Platform / Toolchain |
| :--- | :--- | :--- | :--- |
| **[integration/clang-native](./integration/clang-native/README.md)** | All-features demo: sync, async, async-wait, multicast, signal. Windows or Linux. | `std::thread` | Any C++17 compiler (Clang, GCC, MSVC) |
| **[integration/cmake-subdirectory](./integration/cmake-subdirectory/README.md)** | DelegateMQ added as a CMake dependency the way `docs/BUILD.md` recommends: `FetchContent` (or `add_subdirectory`) and the `DelegateMQ::DelegateMQ` target. Checks the options reach the app, nothing leaks into other targets, and the library works. Windows or Linux. | `std::thread` | Any C++17 compiler, CMake 3.16+ |
| **[integration/no-cmake](./integration/no-cmake/README.md)** | Builds DelegateMQ as a non-CMake project would: one source file (`src/DelegateMQ.cpp`) and options in `DelegateMQConfig.h`, without `DelegateMQ.cmake`. Checks sync, async, signal and timer. Windows or Linux. | `std::thread` | Any C++17 compiler |

## patterns/

| Sample | Description | Threading (`dmq::IThread`) | Platform / Toolchain |
| :--- | :--- | :--- | :--- |
| **[patterns/state-machine](./patterns/state-machine/README.md)** | An automatic door controller as an active-object state machine ([active-fsm](https://github.com/DelegateMQ/active-fsm), fetched into `external/`): events from any thread, timer-driven transitions, a transition signal observed on another thread, and four threads sending events at once. Windows or Linux. | `std::thread` | Any C++17 compiler |
| **[patterns/async-sqlite](./patterns/async-sqlite/README.md)** | A sensor data logger with SQLite on its own thread ([Async-SQLite](https://github.com/DelegateMQ/Async-SQLite), fetched into `external/`): concurrent writers with no lock or `SQLITE_BUSY` retries, a transaction sent to the SQLite thread as one function, a future query, a call with a timeout, and a signal delivered to a display thread. Windows or Linux. | `std::thread` | Any C++17 compiler |

## External libraries

Samples using a third-party library (ZeroMQ, NNG, MQTT, RapidJSON, MessagePack, FTXUI, libserialport, ...) get it from the sandbox that `scripts/01_fetch_repos.py` and `02_build_libs.py` set up. See [Example Ecosystem (Sandbox)](../../docs/BUILD.md#automated-workspace-sandbox-setup).

## Showcase Projects

Larger end-to-end projects that integrate multiple DelegateMQ features across several components. Located alongside (not inside) `sample-projects/`.

| Project | Location | Description |
| :--- | :--- | :--- |
| **Cellutron** | `example/cellutron/` | Multi-processor medical instrument demo with three independent CPUs: GUI (stdlib thread), Controller, and Safety — the latter two run on FreeRTOS (default) or ThreadX (`-DCELLUTRON_RTOS=THREADX`, Linux only), a single switch proving DelegateMQ isolates app code from the RTOS. Integrates DataBus with QoS LVC, Active Objects, `DeadlineSubscription` cross-node heartbeats, Spy Monitor audit logging, and explicit per-thread `FullPolicy`. Start with `python run_cellutron.py` (`--threadx` for the ThreadX build). See [Cellutron README](../cellutron/CELLUTRON.md). |
| **Pumptron** | `example/pumptron/` | Pump controller on a real STM32F4 Discovery board (FreeRTOS) or the FreeRTOS simulator, monitored and commanded from an FTXUI console over a serial DataBus link. Includes a hardware watchdog, crash dumps delivered to the console, and optional MQTT/PlotJuggler output. See [Pumptron README](../pumptron/PUMPTRON.md). |
| **sample-interop** | `example/sample-interop/` | Cross-language interop demo: C++ server publishes `SensorData` and receives `Command`; C# and Python clients subscribe and respond — all via a shared native C++ DLL. Demonstrates that scripting-language clients share the same ACK/timeout reliability logic as the C++ core. See [INTEROP.md](../../docs/INTEROP.md). |

## Build

See [Example Projects](../../docs/BUILD.md#example-ecosystem-sandbox) for details on building and running example projects.

### Building with Clang on Linux

All Linux and cross-platform projects that use `DMQ_THREAD_STDLIB` build cleanly with Clang. Pass the compiler at configure time:

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

This applies to: `transport/in-process`, `integration/clang-native`, `databus/basic`, `databus/multicast`, `integration/no-cmake`, `system-architecture/no-deps`, and `transport/serializer` (its ZeroMQ/NNG builds if those are installed). Windows-only and embedded projects are not applicable.
