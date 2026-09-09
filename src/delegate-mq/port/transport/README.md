# Transport Layer

This directory contains the transport layer implementations for **DelegateMQ**, enabling remote function invocation across various physical media and protocols.

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
* **`arm-lwip-udp`**: Lightweight IP (lwIP) implementation for embedded ARM (FreeRTOS/Bare-metal).
* **`netx-udp`**: Azure RTOS **NetX / NetX Duo** implementation for ThreadX (selected via `DMQ_TRANSPORT_THREADX_UDP`).
* **`zephyr-udp`**: Native **Zephyr Networking** (BSD Socket API) implementation for Zephyr RTOS.

### IPC & Serial
* **`win32-pipe`**: Inter-Process Communication (IPC) using Windows Named Pipes.
* **`serial`**: Serial port (UART/RS-232) transport using **libserialport**.

## Usage

To use a specific transport, simply include the corresponding header in your application and inject it into your `dmq::util::Dispatcher` or `dmq::DelegateRemote`.

```cpp
// Example: Using UDP on Windows
#include "port/transport/win32-udp/Win32UdpTransport.h"

dmq::transport::Win32UdpTransport transport;
transport.Create(dmq::transport::Win32UdpTransport::Type::PUB, "127.0.0.1", 5000);

// Link transport to the dispatcher
dispatcher.SetTransport(&transport);
```
