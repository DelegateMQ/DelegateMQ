# Choosing a Configuration

DelegateMQ has many options: about ten OS ports, over a dozen transports, six serializers. This page answers "I have this system; which do I pick?" One table per choice, each row pointing to the details. The choices are independent: any OS port works with any transport and any serializer.

- [OS port](#os-port)
- [Transport](#transport)
- [Serializer](#serializer)
- [System topology](#system-topology)
- [Embedded build profile](#embedded-build-profile)

Options are set in CMake (`set(DMQ_THREAD ...)` before including `DelegateMQ.cmake`) or, without CMake, as `#define`s in `DelegateMQConfig.h`. See [BUILD.md](BUILD.md#configuration-and-overrides).

## OS Port

Which threads your asynchronous delegates run on. On Windows and Linux CMake picks `DMQ_THREAD_STDLIB` by default.

| Your platform | Use (`DMQ_THREAD_*`) | Notes |
| :--- | :--- | :--- |
| Windows or Linux application | `STDLIB` | `std::thread`; the default on the desktop |
| Native Windows threads (Win32 code base) | `WIN32` | Win32 API (`CreateThread`) instead of `std::thread`; same features as `STDLIB` |
| Raw pthreads | `POSIX` | `pthread_create` instead of `std::thread`; same features as `STDLIB` |
| Qt application | `QT` | Delivers on the Qt event loop |
| FreeRTOS (including ESP-IDF and STM32duino) | `FREERTOS` | Also its Windows/POSIX simulators, for development on the PC |
| ThreadX (Azure RTOS) | `THREADX` | Also its Linux simulator |
| Zephyr | `ZEPHYR` | Also `native_sim`, a Linux process |
| CMSIS-RTOS2 (Keil RTX and others) | `CMSIS_RTOS2` | Also on Zephyr's `native_sim` |
| NuttX | `NUTTX` | Also NuttX's `sim`, a host process |
| Bare metal, no RTOS | `NONE` | Synchronous delegates, signals, timers and `IsrDelegate` (interrupt to main loop); no async threads |
| Your own RTOS or event loop | your `IThread` | Two methods; see [PORTING.md](PORTING.md#dmqithread) |

How each port is verified (CI, simulators, QEMU, boards): [Platform Support](../README.md#platform-support).

## Transport

How messages cross between processes and processors. Not needed between threads of one process: asynchronous delegates and the DataBus do that with no transport or serializer. `DMQ_TRANSPORT_*` selects the one `DelegateMQ.h` includes; desktop transports are header-only, so a node can include others directly and use several at once (as [`databus/relay`](../example/sample-projects/databus/relay/README.md) does).

| Your link | Use | Notes |
| :--- | :--- | :--- |
| Threads in one process | none | Async delegates and the DataBus |
| Processes on one machine | Named pipe (`WIN32_PIPE`), Unix socket (`LINUX_UDS`), or shared memory (`Win32ShmTransport`, `LinuxShmTransport`) | Shared memory is the fastest; both sides must use the same `DMQ_SHM_SLOT_COUNT` and `DMQ_SHM_MAX_PAYLOAD`. Pair it with `raw::Serializer` when both sides are the same build. Sample: [transport/shm-ring](../example/sample-projects/transport/shm-ring/README.md) |
| Cores of a dual-core MCU (AMP) | `ShmRingTransport` | No OS calls; you supply the chip's wake-up (`IShmSignal`), the linker placement and cache setup |
| Ethernet or Wi-Fi, desktop | UDP (`WIN32_UDP`, `LINUX_UDP`), TCP (`WIN32_TCP`, `LINUX_TCP`) | TCP for reliable, ordered delivery with no retry layer. UDP for low latency or multicast, with `ReliableTransport` for ACK and retry where needed |
| Ethernet or Wi-Fi, MCU | lwIP (`ARM_LWIP_UDP`, `ARM_LWIP_NETCONN_UDP`), NetX Duo (`THREADX_UDP`), Zephyr sockets (`ZEPHYR_UDP`) | lwIP also covers ESP-IDF |
| Serial port / UART | `SERIAL_PORT` (PC, libserialport), `STM32_UART` (STM32 HAL) | Framed and CRC-checked |
| Through a broker, IoT | MQTT (`MQTT`) | For DataBus topics as JSON to MQTT tools (Node-RED, Grafana), see `MqttBridge` in [TOOLS.md](../tools/TOOLS.md#using-mqtt-tools) |
| Messaging library already in use | ZeroMQ (`ZEROMQ`), NNG (`NNG`) | |
| Anything else | your `ITransport` | `Send()` and `Receive()`; see [the transport README](../src/delegate-mq/port/transport/README.md) |

The shared-memory transports have no `DMQ_TRANSPORT_*` option: include their header directly, beside whichever transport is selected (`DMQ_TRANSPORT_NONE` if there is no other).

Every transport: [port/transport/README.md](../src/delegate-mq/port/transport/README.md). Samples: [transport/](../example/sample-projects/transport/README.md).

## Serializer

How arguments become bytes for a transport. `DMQ_SERIALIZE_*` selects one; `raw::Serializer` is always available beside it, and each message type (remote delegate signature or DataBus topic) can use either.

| You need | Use (`DMQ_SERIALIZE_*`) | Notes |
| :--- | :--- | :--- |
| No third-party library, C++ on both ends | built-in (`SERIALIZE`) | The default; one `serialize::I` per message class |
| C# or Python peers | MessagePack (`MSGPACK`) | What [interop](INTEROP.md) uses |
| Human-readable messages, JSON tools | RapidJSON (`RAPIDJSON`) | |
| A library your code already uses | Cereal (`CEREAL`), Bitsery (`BITSERY`) | Bitsery is compact binary |
| Same build on both ends (cores of one chip, processes), lowest cost | `raw::Serializer` | Copies the bytes; trivially copyable types with fixed-width fields only |
| No remote messages, only `raw::Serializer`, or your own `ISerializer` | `NONE` | Nothing selected; `raw::Serializer` is still available |

Details: [port/serialize/README.md](../src/delegate-mq/port/serialize/README.md). Every serializer works with every transport: [transport/serializer](../example/sample-projects/transport/serializer/README.md) builds any pair.

## System Topology

How the pieces combine. Every row uses the same `Publish`/`Subscribe` (or remote delegate) code in the application; only the setup differs.

| Your system | Use | Sample |
| :--- | :--- | :--- |
| One processor, many threads | Async delegates, signals, DataBus; no transport | [databus/local](../example/sample-projects/databus/local/README.md) |
| Two nodes (PC ⇄ PC, PC ⇄ MCU) | DataBus with `NetworkNode` (UDP) or a `Participant` on any transport; or remote delegates (`RemoteChannel`) for point-to-point calls | [databus/basic](../example/sample-projects/databus/basic/README.md), [Pumptron](../example/pumptron/PUMPTRON.md) |
| One publisher, many subscribers | DataBus over UDP multicast | [databus/multicast](../example/sample-projects/databus/multicast/README.md) |
| Several nodes on one network | `NetworkNode` with a peer per node | [Cellutron](../example/cellutron/CELLUTRON.md) (three nodes) |
| Nodes on different links | A gateway node on both links relays topics between them (`AddRelayTopic`, `Delivery::RELAY`) | [databus/relay](../example/sample-projects/databus/relay/README.md) |
| PC ⇄ dual-core MCU | The network core relays between Ethernet and shared memory to the real-time core | [databus/relay](../example/sample-projects/databus/relay/README.md) with `--shm` (desktop rehearsal); [transport/shm-ring](../example/sample-projects/transport/shm-ring/README.md) for shared memory alone |
| Client/server RPC | Remote delegates with `RemoteDispatcher` | [system-architecture](../example/sample-projects/system-architecture/README.md) |
| Monitoring from C#, Python or MQTT tools | MessagePack interop, or `MqttBridge` | [databus/interop](../example/sample-projects/databus/interop/README.md), [databus/mqtt-gateway](../example/sample-projects/databus/mqtt-gateway/README.md) |

Topologies in depth: [DATABUS.md](DATABUS.md#topologies).

## Embedded Build Profile

Common settings for a microcontroller, on top of the OS port and transport:

| You want | Set |
| :--- | :--- |
| No exceptions | Build with `-fno-exceptions`; `DMQ_ASSERTS` turns on by itself (faults call `FaultHandler`) |
| No RTTI | Build with `-fno-rtti` (`DMQ_NO_RTTI` follows) |
| Deterministic allocation, no fragmentation | `DMQ_ALLOCATOR` (fixed-block pools) |
| No heap at all | `DMQ_ALLOCATOR` with `DMQ_XALLOCATOR_STATIC_POOLS` and pool sizes from `xalloc_print_pool_config()` |
| Smaller flash | `DMQ_DATABUS` `OFF` (already the default on RTOS and bare-metal ports), `DMQ_SERIALIZE_NONE` and `DMQ_TRANSPORT_NONE` if unused, `DMQ_UTIL` `OFF` without `Timer`, monitors or `ReliableTransport`. And no iostreams in your firmware ([pitfall](APP-GUIDE.md#pitfalls)) |

Every option, with its default: [`DelegateMQConfig_Template.h`](../src/delegate-mq/delegate/DelegateMQConfig_Template.h).
