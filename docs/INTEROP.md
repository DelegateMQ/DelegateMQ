# DelegateMQ Cross-Language Interop

DelegateMQ is a C++ library, but it supports first-class interoperability with **C#** and **Python** (and other languages) through a **Shared Native Core** architecture.

This ensures that all languages benefit from the same high-performance networking, reliability logic (ACKs/timeouts), and wire-protocol framing without re-implementing them in every language.

## Architecture: Shared Native Core

The interop system is split into three layers:

1.  **Native Core (`DmqInterop.dll`)**: A C++ shared library that wraps the DelegateMQ `Win32UdpTransport` and `TransportMonitor`. It handles the "hot" receive loop on a native thread.
2.  **Thin Language Wrappers**: Lightweight libraries in C# (`P/Invoke`) and Python (`ctypes`) that call the DLL and handle high-level data serialization.
3.  **Serialization (MessagePack)**: A high-performance binary format used to exchange data structures between languages.

### Why this Architecture?

-   **Reliability**: Scripting languages inherit C++ reliability (sequence numbers, ACKs, retries) for free.
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
// Initialize and start the UDP transport
int DmqInterop_Start(const char* remoteHost, int recvPort, int sendPort);

// Register a callback for a specific Remote ID
void DmqInterop_RegisterCallback(uint16_t remoteId, DmqMessageCallback cb);

// Register a callback for internal library errors and faults
void DmqInterop_RegisterErrorCallback(DmqErrorCallback cb);

// Send raw bytes to a Remote ID (DLL handles framing)
int DmqInterop_Send(uint16_t remoteId, const uint8_t* data, uint32_t len);

// Stop and cleanup
void DmqInterop_Stop();
```

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

## Error Handling

Internal library errors, faults (assertions), and C++ exceptions are captured by the Native Core and reported back to the language wrappers via a dedicated error callback.

### 1. C# Usage
```csharp
bus.RegisterErrorCallback(msg => {
    Console.WriteLine($"[DMQ ERROR] {msg}");
});
```

### 2. Python Usage
```python
def on_error(msg):
    print(f"[DMQ ERROR] {msg}")

bus.register_error_callback(on_error)
```

---

## Language Support

### C# / .NET
- **Location**: `interop/csharp/`
- **Requirement**: .NET 6+ or .NET 8+
- **Usage**: Reference the `DelegateMQ.Interop` project and use the `DmqDataBus` class. It is `IDisposable` and provides a strongly-typed `RegisterCallback<T>` method.

### Python
- **Location**: `interop/python/`
- **Requirement**: Python 3.8+, `pip install msgpack`
- **Usage**: Import the `dmq_databus` module. It uses `ctypes` to load the DLL and provides an asynchronous-friendly callback mechanism.

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
