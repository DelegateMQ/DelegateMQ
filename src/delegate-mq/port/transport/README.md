# Transports (DelegateMQ Abstraction Layer)

This directory contains the transport part of the **DelegateMQ Abstraction Layer**: the `ITransport` implementations that enable remote function invocation across various physical media and protocols.

## Core Interfaces

* **`dmq::transport::ITransport`**: The abstract base class that all transport implementations must inherit from. Defines the `Send()` and `Receive()` contract.
* **`dmq::transport::DmqHeader`**: Defines the protocol header structure (Marker, ID, Sequence Number, Length) used for framing messages.
* **`dmq::transport::ITransportMonitor`**: Interface for reliability monitoring (ACKs, timeouts, and retries).

## Implementations

The subdirectories contain ready-to-use transport implementations for specific platforms and libraries:

### Network (IP-based)
* **`zeromq`**: High-performance asynchronous messaging using **ZeroMQ**.
* **`nng`**: Scalability protocols using **NNG** (Nanomsg Next Gen).
* **`mqtt`**: Publish/Subscribe messaging using **Paho MQTT**.
* **`linux-tcp` / `linux-udp`**: Standard BSD socket implementations for Linux.
* **`win32-tcp` / `win32-udp`**: Winsock implementations for Windows.
* **`arm-lwip-udp`**: Lightweight IP (lwIP) socket-API UDP implementation for embedded targets (needs `LWIP_SOCKET` and `LWIP_SO_RCVTIMEO`). Built and run on the host, over lwIP's Unix port and loopback interface, by `example/sample-projects/transport/serializer` (`-DSAMPLE_TRANSPORT=lwip`), and on an ESP32-S3 over Wi-Fi (ESP-IDF's network stack is lwIP) by `example/sensor-node`. Despite the name, nothing in it is ARM specific.
* **`arm-lwip-netconn-udp`**: lwIP netconn-API UDP implementation for RTOS targets (selected via `DMQ_TRANSPORT_ARM_LWIP_NETCONN_UDP`). Built and run the same way (`-DSAMPLE_TRANSPORT=lwip-netconn`).
* **`netx-udp`**: Azure RTOS **NetX / NetX Duo** implementation for ThreadX (selected via `DMQ_TRANSPORT_THREADX_UDP`). Built and run on ThreadX's Linux simulation port, over NetX Duo's loopback interface, by `example/sample-projects/transport/netx-udp`.
* **`zephyr-udp`**: Native **Zephyr Networking** (BSD Socket API) implementation for Zephyr RTOS.

### IPC & Serial
* **`win32-pipe`**: Inter-Process Communication (IPC) using Windows Named Pipes.
* **`linux-uds`**: Inter-Process Communication (IPC) using Linux Unix domain datagram sockets (`AF_UNIX`, `SOCK_DGRAM`). Duplex, path-addressed, no start-order dependency. A stale socket file left by a crashed process is reused; a path still bound by a live process makes `Create()` fail. Socket access follows the directory's permissions, so prefer a per-user directory such as `$XDG_RUNTIME_DIR` over `/tmp` when other local users must not be able to send. Selected via `DMQ_TRANSPORT_LINUX_UDS`.
* **`shm-ring`**: Shared memory between two cores of an AMP MCU, two processes, or two threads (`ShmRingTransport`). Two lock-free single-producer, single-consumer rings of fixed-size slots in one shared control block, no locks between the sides, no heap, no OS calls. The peer is woken through an `IShmSignal` (a cross-core interrupt, an event, a semaphore), or not at all (polling). Sized by `DMQ_SHM_SLOT_COUNT` / `DMQ_SHM_MAX_PAYLOAD`; a full ring fails `Send()` and is counted. Failures return a `ShmRingError`, which a remote delegate's error handler receives as the aux code of `ERR_DISPATCH`; a malformed received slot goes to `SetRecvErrorHandler()`. Header-only, not selected by `DMQ_TRANSPORT_*` and not included by `DelegateMQ.h`: include `port/transport/shm-ring/ShmRingTransport.h` beside any other transport.
* **`win32-shm`** / **`linux-shm`**: `ShmRingTransport` between two processes: a named file mapping and events (Windows), or `shm_open` and named semaphores (Linux). Run by `example/sample-projects/transport/shm-ring`.
* **`framed-stream`**: DelegateMQ messages over any byte stream: UART, USB-CDC, RS-485, a BLE serial service, a PC serial port (`FramedStreamTransport`). The frame (`0xAA 0x55` marker, header, payload, CRC-16), resync after line noise, locking, ACKs and timeouts are here once (`FrameCodec.h`, unit-tested on the PC), with no OS or hardware code. A link supplies an `IByteStream`: `Write()` bytes, and `Read()` what has arrived with a timeout. For an interrupt-driven UART, `IsrByteStream` is the receive side: the interrupt calls `PutFromIsr()` and `Read()` sleeps on a `dmq::Semaphore` (any RTOS port; it polls on bare metal). `Receive()` returns `RECEIVE_NO_DATA` after `DMQ_UART_RECV_TIMEOUT_MS` with nothing; a dropped frame (bad CRC, bad header, oversized, truncated) returns -1, is counted and goes to `SetRecvErrorHandler()` with its `StreamError`. A new link is an adapter of about 50 lines, like the two below. Header-only; include it directly.
* **`serial`**: A PC serial port (`SerialTransport`): a `framed-stream` adapter over **libserialport**.
* **`stm32-uart`**: An STM32 HAL UART, interrupt-driven (`Stm32UartTransport`, selected via `DMQ_TRANSPORT_STM32_UART`): a `framed-stream` adapter, on any RTOS port. It includes the first family HAL on the include path (H7, F4, F7, G4, L4), or the header named by `DMQ_STM32_HAL_HEADER`.

## Usage

To use a specific transport, simply include the corresponding header in your application and inject it into your `dmq::Dispatcher` or `dmq::DelegateRemote`.

```cpp
// Example: Using UDP on Windows
#include "port/transport/win32-udp/Win32UdpTransport.h"

dmq::transport::Win32UdpTransport transport;
transport.Create(dmq::transport::Win32UdpTransport::Type::PUB, "127.0.0.1", 5000);

// Link transport to the dispatcher
dispatcher.SetTransport(&transport);
```
