# DelegateMQ Cross-Language Interop

DelegateMQ is a C++ library, but it supports first-class interoperability with **C#** and **Python** (and other languages) through a **Shared Native Core** architecture.

This ensures that all languages benefit from the same high-performance networking, reliability logic (ACKs/timeouts), and wire-protocol framing without re-implementing them in every language.

## Architecture: Shared Native Core

The interop system is split into three layers:

1.  **Native Core (`DmqInterop.dll`)**: A C++ shared library that wraps the DelegateMQ transports and the reliability stack (`TransportMonitor`, `RetryMonitor`, `ReliableTransport`, the same stack a C++ `NetworkNode` uses in `RELIABLE` mode). It runs the "hot" receive loop and the ACK/retry processing on native threads.
2.  **Thin Language Wrappers**: Lightweight libraries in C# (`P/Invoke`) and Python (`ctypes`) that call the DLL and handle high-level data serialization.
3.  **Serialization (MessagePack)**: A high-performance binary format used to exchange data structures between languages.

### Why this Architecture?

-   **Reliability**: Scripting languages inherit C++ reliability for outgoing messages (sequence numbers, ACKs, timeouts, retries) for free, with a per-message delivery status callback.
-   **Performance**: Raw UDP handling and protocol parsing occur on a native thread, avoiding the Python GIL and .NET Garbage Collector overhead.
- **Consistency**: The wire protocol is implemented once, ensuring all clients behave identically.

---

## Platform Support

The DelegateMQ Interop system is designed to be highly portable across desktop, server, and embedded environments.

### 1. Desktop & Server
The `DmqInterop` native core is fully supported and tested on:
- **Windows**: Built as `DmqInterop.dll` using MSVC or MinGW.
- **Linux**: Built as `libDmqInterop.so` using GCC or Clang.
- **macOS**: Can be built as `libDmqInterop.dylib`.

### 2. Embedded & RTOS
While the language wrappers (C#/Python) typically run on a "Host" (PC/Server), the **Native Core** or a **Pure C++ DelegateMQ app** can run on:
- **RTOS**: FreeRTOS, Zephyr, ThreadX, CMSIS-RTOS2.
- **Bare Metal**: Systems with no OS (using custom timers and polling).

This allows a C# application on Windows to communicate seamlessly with a FreeRTOS-based embedded device using the same shared protocol and reliability layer.

---

## Native Interop DLL (`native/`)

The core of the system is the `DmqInterop` DLL. It exposes a C-compatible API.

### C-API (`DmqInterop.h`)
```cpp
// Optional, before Start: outgoing reliability (default: on, 2 s timeout, 3 retries)
int DmqInterop_SetReliability(int enabled, int timeoutMs, int maxRetries);

// Initialize and start the transports and native threads
int DmqInterop_Start(const char* remoteHost, int recvPort, int sendPort, const char* multicastGroup);

// Register a callback for a specific Remote ID (NULL unregisters)
void DmqInterop_RegisterCallback(uint16_t remoteId, DmqMessageCallback cb, void* context);

// Per-message delivery status: callback(context, remoteId, seqNum, DmqSendStatus)
void DmqInterop_RegisterStatusCallback(DmqStatusCallback cb, void* context);

// Errors: callback(context, DmqErrorCode, remoteId or 0, message)
void DmqInterop_RegisterErrorCallback(DmqErrorCallback cb, void* context);

// Send raw bytes to a Remote ID (DLL handles framing); returns the sequence number via seqNum
int DmqInterop_Send(uint16_t remoteId, const uint8_t* data, uint32_t len, uint16_t* seqNum);

// Stop and cleanup (blocks until native threads exit)
void DmqInterop_Stop();
```

All callbacks may be registered before or after `Start`. Each `Register*` function takes a `void* context` that is passed back unchanged as the callback's first argument (e.g. `DmqMessageCallback(void* context, uint16_t remoteId, const uint8_t* data, uint32_t len)`), so a caller can reach its own state without globals; pass NULL if unused. Callbacks run on native threads (the receive thread, the ACK/retry thread, or, for an immediate send failure, the thread calling `Send`), so keep them short and thread-safe.

### Reliability and Delivery Status

Incoming messages (the multicast receive channel) are best-effort. Outgoing messages are reliable by default:

1.  `Send` assigns a sequence number and returns it to the caller.
2.  The peer ACKs the message (see [ACK Convention](#ack-convention)). The DLL reads ACKs on its send socket.
3.  If no ACK arrives within the timeout, the message is resent, up to `maxRetries` times.
4.  Each outcome is reported through the status callback, tagged with the sequence number:

| Status | Meaning |
| :--- | :--- |
| `ACKED` | The peer acknowledged the message. Final. |
| `TIMEOUT` | An attempt timed out. A retry follows unless `DELIVERY_FAILED` is reported next. |
| `DELIVERY_FAILED` | Abandoned: retries exhausted, or the send failed immediately. Final. |

A message that times out after the peer already received it is resent. DelegateMQ receivers discard the duplicate by sequence number, so a command is not applied twice.

Call `DmqInterop_SetReliability(0, 0, 0)` for fire-and-forget sends with no status callbacks, when the peer doesn't send ACKs. `maxRetries = 0` tracks ACKs without resending. `DmqInterop_Stop` drops messages still awaiting an ACK without a final status.

### Building the DLL
```powershell
cd interop/native
cmake -B build . -DDMQ_TRANSPORT=DMQ_TRANSPORT_WIN32_UDP
cmake --build build --config Release
```

---

## Transport Options

The Native Interop DLL can be compiled with different transport backends to suit your network environment.

### 1. UDP (Default)
Standard low-latency UDP communication.
- **Windows**: Use `-DDMQ_TRANSPORT=DMQ_TRANSPORT_WIN32_UDP`
- **Linux**: Use `-DDMQ_TRANSPORT=DMQ_TRANSPORT_LINUX_UDP`
- **Address Format**: `DmqInterop_Start` expects a hostname or IP string (e.g., `"127.0.0.1"`).

### 2. ZeroMQ (ZMQ)
Enterprise-grade messaging with built-in reliability and patterns.
- **Build Flag**: `-DDMQ_TRANSPORT=DMQ_TRANSPORT_ZEROMQ`
- **Address Format**: Pass the hostname/IP as usual (e.g., `"127.0.0.1"`); the DLL constructs the ZMQ connection string internally (e.g., `tcp://127.0.0.1:8000`) using the host and port parameters.
- **Dependency**: Requires `libzmq` to be available in your CMake path.

---

## Build Configuration

You can customize the DLL behavior using the following CMake variables:

| Variable | Values | Description |
| :--- | :--- | :--- |
| `DMQ_TRANSPORT` | `DMQ_TRANSPORT_WIN32_UDP`, `DMQ_TRANSPORT_LINUX_UDP`, `DMQ_TRANSPORT_ZEROMQ` | Selects the physical network layer. |
| `DMQ_ASSERTS` | `ON`, `OFF` | Enables/Disables internal library assertions. |
| `DMQ_LOG` | `ON`, `OFF` | Enables/Disables internal library logging. |
| `DMQ_FORCE_OPTIMIZE_DEBUG` | `ON`, `OFF` | Forces `-Os` template optimization in GCC/Clang unoptimized Debug builds. |
| `CMAKE_BUILD_TYPE` | `Debug`, `Release` | Standard CMake build configuration. |

---

## Data Synchronization & Schema Management

Since DelegateMQ interop involves multiple languages, keeping data structures (structs/classes) in sync is critical. There are three primary strategies for managing these schemas:

### Strategy 1: Field Order Convention (Default)

The simplest approach, used in the samples, relies on a **shared field order** via MessagePack arrays. This avoids an external IDL but requires manual synchronization.

1.  **C++**: Use `MSGPACK_DEFINE(field1, field2, ...)`.
2.  **C#**: Use `[Key(0)]`, `[Key(1)]`, etc., matching the C++ order.
3.  **Python**: Access elements by index `data[0]`, `data[1]`.

**Pros**: No extra build steps, zero-overhead.
**Cons**: Error-prone for large teams; adding/removing fields requires updating all languages.

### Strategy 2: External IDL (e.g., Protobuf)

For complex projects, you can use an Interface Definition Language (IDL) like **Protocol Buffers (Protobuf)** or **FlatBuffers**.

1.  **Define**: Create a `.proto` file defining your messages.
2.  **Generate**: Use `protoc` to generate native classes for C++, C#, and Python.
3.  **Integrate**:
    -   In C++, serialize the Protobuf object to a string/buffer and send via `DmqInterop_Send`.
    -   In the wrappers, pass the raw bytes to the Protobuf library for deserialization.

**Pros**: Strong typing, version compatibility (backward/forward), automated code generation.
**Cons**: Requires a schema compiler and adding Protobuf dependencies to all environments.

### Strategy 3: Single-Source Code Generation

If you prefer MessagePack but want more safety, you can use a custom script (e.g., Python/Jinja2) to generate the C++, C#, and Python definitions from a single JSON/YAML manifest.

**Pros**: Custom-tailored to your project, maintains MessagePack performance.
**Cons**: Requires maintaining a custom generation script.

---

## Error Handling and Delivery Status

Errors reach the language wrappers through an error callback with a code, the related remote ID (0 if none) and a message. Per-message delivery outcomes use the separate status callback, so an app can tell "still retrying" from "permanently failed" from "something is wrong with the interop layer".

| Error code | Meaning |
| :--- | :--- |
| `EXCEPTION` | A native exception was caught (message has details). |
| `CAP_EXCEEDED` | Too many unacknowledged messages outstanding; new sends fail until ACKs or timeouts free slots. |
| `PENDING_EXCEEDED` | Timed-out messages are piling up faster than they can be processed (link down or peer overloaded). |
| `FAULT` | Library fault (failed assertion). The process aborts after the callback. |
| `WATCHDOG` | A thread watchdog expired. The process aborts after the callback. |
| `CALLBACK` | Raised by the wrappers: an exception in a user callback, including a payload that fails to deserialize. |
| `TRANSPORT` | A transport couldn't be created (bind or multicast join failed); `Start` fails. |

The wrappers never let an exception from a user callback escape into native code. In C#, that would terminate the process; in Python, ctypes would print and discard it. Instead, it is reported as `CALLBACK`. Without an error callback, errors are written to stderr.

### 1. C# Usage
```csharp
bus.RegisterStatusCallback((remoteId, seq, status) => {
    if (status == SendStatus.DeliveryFailed)
        Console.WriteLine($"[DMQ] message {seq} to {remoteId} was not delivered");
});
bus.RegisterErrorCallback((code, remoteId, msg) => {
    Console.WriteLine($"[DMQ ERROR] {code}: {msg}");
});

ushort seq = bus.Send(CommandId, cmd);   // matches the seq in status callbacks
```

### 2. Python Usage
```python
from dmq_databus import SendStatus

def on_status(remote_id, seq, status):
    if status == SendStatus.DELIVERY_FAILED:
        print(f"[DMQ] message {seq} to {remote_id} was not delivered")

def on_error(code, remote_id, msg):
    print(f"[DMQ ERROR] {code.name}: {msg}")

bus.register_status_callback(on_status)
bus.register_error_callback(on_error)

seq = bus.send(COMMAND_ID, [250])   # matches the seq in status callbacks
```

---

## Language Support

### C# / .NET
- **Location**: `interop/csharp/`
- **Requirement**: .NET 6+ or .NET 8+
- **Usage**: Reference the `DelegateMQ.Interop` project and use the `DmqDataBus` class. It is `IDisposable` and provides a strongly-typed `RegisterCallback<T>` method, `SetReliability`, `RegisterStatusCallback`, and `Send` returning the sequence number.

### Python
- **Location**: `interop/python/`
- **Requirement**: Python 3.8+, `pip install msgpack`
- **Usage**: Import the `dmq_databus` module. It uses `ctypes` to load the DLL and provides `set_reliability`, message/status/error callbacks with `SendStatus` and `ErrorCode` enums, and `send` returning the sequence number. `DmqDataBus` is also a context manager that stops the native threads on exit.

---

## Wire Protocol

If you are implementing a custom transport or a new language wrapper without using the `DmqInterop` DLL, you must adhere to the standard DelegateMQ wire protocol.

### 8-Byte Binary Header
All messages begin with a fixed 8-byte header, followed immediately by the payload. All fields are in **Network Byte Order (Big Endian)**.

| Offset | Field | Size | Description |
| :--- | :--- | :--- | :--- |
| 0 | Marker | 2 bytes | Static sync marker: `0xAA55`. |
| 2 | ID | 2 bytes | `DelegateRemoteId` (The "Topic" ID). |
| 4 | SeqNum | 2 bytes | Monotonically increasing sequence number. |
| 6 | Length | 2 bytes | Length of the payload (excluding header). |

### ACK Convention
DelegateMQ uses explicit ACKs to provide reliability over connectionless transports (like UDP).
- **ACK ID**: The `DelegateRemoteId` for an ACK message is always `0`.
- **Payload**: An ACK message has a **zero-length payload**.
- **Sequence Matching**: To acknowledge a message, send a header with `ID=0` and a `SeqNum` matching the sequence number of the message being acknowledged.

---

## Complete Demo

A coordinated 3-language demonstration (C++ Server, C# Client, Python Client) is available in:
**[example/sample-interop/](../example/sample-interop/README.md)**
