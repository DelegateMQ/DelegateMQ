# Sample Projects

## Feature & Toolchain Demos

The following projects demonstrate DelegateMQ delegate types (sync, async, asyncwait, multicast, signal) on specific platforms or compilers. No remote transport is involved.

| Project Name | Description | Threading (`dmq::IThread`) | Platform / Toolchain |
| :--- | :--- | :--- | :--- |
| **[clang-native](./clang-native/)** | All-features demo: sync, async, asyncwait, multicast, signal. Windows or Linux. | `std::thread` | Any C++17 compiler (Clang, GCC, MSVC) |
| **[atfe-armv7m-bare-metal](./atfe-armv7m-bare-metal/)** | ATfE (Clang/picolibc) bare-metal example for Armv7-M, runs on QEMU. | None | ATfE Clang, picolibc |
| **[bare-metal-arm](./bare-metal-arm/)** | ARM GCC bare-metal example for Cortex-M4, runs on QEMU. | None | ARM GCC |
| **[keil-bare-metal](./keil-bare-metal/)** | Bare-metal example for ARM Cortex-M4. | None | Keil MDK (ARMCLANG) |
| **[stm32-freertos](./stm32-freertos/)** | Embedded FreeRTOS example for STM32F4 Discovery. | FreeRTOS | STM32Cube / ARM GCC |

## Remote Delegate Examples

Remote delegates invoke a target function that runs in a separate processor or process. Transport and serialization libraries are configurable. Each sample project focuses on a transport-serialization pair, but you can freely mix and match any transport with any serializer.

### No External Dependencies

The following remote delegate projects have no external library dependencies. They rely only on the standard system APIs (Windows API, POSIX, etc.) or headers included within the repository.

| Project Name | Description | Threading (`dmq::IThread`) | Serialization (`dmq::ISerializer`) | Transport (`dmq::IDispatcher`) |
| :--- | :--- | :--- | :--- | :--- |
| **[bare-metal-remote](./bare-metal-remote/)** | Simple remote delegate example on Windows and Linux. | `std::thread` | `operator<<` / `operator>>` | `std::stringstream` |
| **[databus](./databus/)** | Distributed sensor/actuator system using `dmq::databus::DataBus` over UDP. | `std::thread` | `dmq::Serializer` class | UDP Socket |
| **[databus-freertos](./databus-freertos/)** | FreeRTOS server (Win32 simulator, 32-bit) publishing sensor data to a Linux/Windows client over UDP, with the client sending rate-control commands back. Demonstrates mixed-platform DataBus with the FreeRTOS port. | FreeRTOS | `dmq::Serializer` class | UDP Socket |
| **[databus-multicast](./databus-multicast/)** | One-to-many distribution using `dmq::databus::DataBus` over UDP Multicast. | `std::thread` | `dmq::Serializer` class | UDP Multicast |
| **[freertos-bare-metal](./freertos-bare-metal/)** | FreeRTOS Windows port example (32-bit build). | FreeRTOS | `operator<<` / `operator>>` | `std::stringstream` |
| **[linux-tcp-serializer](./linux-tcp-serializer/)** | Simple TCP remote delegate app on Linux. | `std::thread` | `dmq::Serializer` class | Linux TCP Socket |
| **[linux-udp-serializer](./linux-udp-serializer/)** | Simple UDP remote delegate app on Linux. | `std::thread` | `dmq::Serializer` class | Linux UDP Socket |
| **[system-architecture-no-deps](./system-architecture-no-deps/)** | Complex remote delegate client/server apps using UDP on Windows or Linux. | `std::thread` | `operator<<` / `operator>>` | UDP Socket |
| **[win32-pipe-serializer](./win32-pipe-serializer/)** | Windows Named Pipe remote delegate app. | `std::thread` | `dmq::Serializer` class | Windows Pipe |
| **[win32-tcp-serializer](./win32-tcp-serializer/)** | Windows TCP Socket remote delegate app. | `std::thread` | `dmq::Serializer` class | Windows TCP Socket |
| **[win32-udp-serializer](./win32-udp-serializer/)** | Windows UDP Socket remote delegate app. | `std::thread` | `dmq::Serializer` class | Windows UDP Socket |

### External Dependencies

The following projects require external 3rd party library support (e.g., ZeroMQ, MQTT, RapidJSON, etc.). See [Examples Setup](../../docs/BUILD.md#automated-workspace-sandbox-setup) for external library installation setup.

| Project Name | Description | Threading (`dmq::IThread`) | Serialization (`dmq::ISerializer`) | Transport (`dmq::IDispatcher`) |
| :--- | :--- | :--- | :--- | :--- |
| **[databus-interop](./databus-interop/)** | Cross-language communication between C++ server and Python/C# clients using `dmq::databus::DataBus` and MessagePack. | `std::thread` | MessagePack | UDP Socket |
| **[databus-shapes](./databus-shapes/)** | Graphical TUI Shapes Demo using `dmq::databus::DataBus`, UDP Multicast, and FTXUI. | `std::thread` | `dmq::Serializer` class | UDP Multicast |
| **[mqtt-rapidjson](./mqtt-rapidjson/)** | Remote delegate example with MQTT and RapidJSON (Client/Server). | `std::thread` | RapidJSON | MQTT |
| **[nnq-bitsery](./nnq-bitsery/)** | Remote delegate using NNG and Bitsery. | `std::thread` | Bitsery | NNG |
| **[serialport-serializer](./serialport-serializer/)** | Remote delegate using libserialport. | `std::thread` | `dmq::Serializer` class | `libserialport` |
| **[system-architecture](./system-architecture/)** | System architecture example with dependencies. | `std::thread` | Various | Various |
| **[system-architecture-python](./system-architecture-python/)** | Python binding example (ZeroMQ). | `std::thread` | N/A | Python / ZeroMQ |
| **[zeromq-bitsery](./zeromq-bitsery/)** | ZeroMQ transport with Bitsery serialization. | `std::thread` | Bitsery | ZeroMQ |
| **[zeromq-cereal](./zeromq-cereal/)** | ZeroMQ transport with Cereal serialization. | `std::thread` | Cereal | ZeroMQ |
| **[zeromq-msgpack-cpp](./zeromq-msgpack-cpp/)** | ZeroMQ transport with MessagePack. | `std::thread` | MessagePack | ZeroMQ |
| **[zeromq-rapidjson](./zeromq-rapidjson/)** | ZeroMQ transport with RapidJSON. | `std::thread` | RapidJSON | ZeroMQ |
| **[zeromq-serializer](./zeromq-serializer/)** | ZeroMQ transport with custom `dmq::Serializer` class. | `std::thread` | `dmq::Serializer` class | ZeroMQ |

## Showcase Projects

Larger end-to-end projects that integrate multiple DelegateMQ features across several components. Located alongside (not inside) `sample-projects/`.

| Project | Location | Description |
| :--- | :--- | :--- |
| **Cellutron** | `example/cellutron/` | Multi-processor medical instrument demo with three independent CPUs: GUI (stdlib thread), Controller, and Safety (both FreeRTOS on Windows simulation). Integrates DataBus with QoS LVC, Active Objects, `DeadlineSubscription` cross-node heartbeats, Spy Monitor audit logging, and explicit per-thread `FullPolicy`. Start with `python run_cellutron.py`. See [Cellutron README](../cellutron/CELLUTRON.md). |
| **sample-interop** | `example/sample-interop/` | Cross-language interop demo: C++ server publishes `SensorData` and receives `Command`; C# and Python clients subscribe and respond — all via a shared native C++ DLL. Demonstrates that scripting-language clients share the same ACK/timeout reliability logic as the C++ core. See [INTEROP.md](../../docs/INTEROP.md). |

## Build

See [Example Projects](../../docs/BUILD.md#example-ecosystem-sandbox) for details on building and running example projects.

### Building with Clang on Linux

All Linux and cross-platform projects that use `DMQ_THREAD_STDLIB` build cleanly with Clang. Pass the compiler at configure time:

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

This applies to: `bare-metal-remote`, `clang-native`, `databus`, `databus-multicast`, `linux-tcp-serializer`, `linux-udp-serializer`, `system-architecture-no-deps`, and the ZeroMQ-based projects (if ZeroMQ is installed). Windows-only and embedded projects are not applicable.
