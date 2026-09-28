# DataBus Interop Sample

Demonstrates cross-language communication between a **C++ DataBus server** and
clients written in **Python** and **C#**.

The C++ server periodically publishes sensor/actuator data (`DataMsg`) and
accepts a polling-rate command (`CommandMsg`) from any client.  Both the Python
and C# clients use the generic `DmqDataBus` library from `interop/` — no
DelegateMQ C++ code is needed on the client side.

```
interop/python/       — reusable Python DataBus library (DmqDataBus, Serializer)
interop/csharp/       — reusable C# DataBus library  (DmqDataBus, Serializer)

example/sample-projects/databus-interop/
├── server/           — C++ DataBus server (CMake)
├── python-client/    — Python client
└── csharp-client/    — C# client (.NET 8)
```

---

## Quick Start

Start the C++ server first, then run either (or both) clients.

### 1. Build and run the C++ server

**Prerequisites:** CMake 3.16+, a C++17 compiler, msgpack-c header-only library
on the include path (or fetched automatically via the parent CMake setup).

```bash
cd example/sample-projects/databus-interop/server
cmake -B build
cmake --build build --config Release
```

**Windows:**
```
build\Release\delegate_databus_interop_server.exe [duration_seconds]
```

**Linux:**
```bash
./build/delegate_databus_interop_server [duration_seconds]
```

Default duration is 30 seconds.  The server prints one line per published
`DataMsg` and one line each time it receives a `CommandMsg`.

---

### 2. Run the Python client

**Prerequisites:** Python 3.7+, `msgpack` package.

```bash
pip install msgpack
cd example/sample-projects/databus-interop/python-client
python main.py [duration_seconds]
```

The client prints each received `DataMsg` and confirms the `CommandMsg` it
sends.

---

### 3. Run the C# client

**Prerequisites:** .NET 10 SDK, and the native interop DLL built first (`interop/native`, see [INTEROP.md](../../../docs/INTEROP.md)); the project copies it next to the executable.

```bash
cd example/sample-projects/databus-interop/csharp-client
dotnet run [duration_seconds]
```

`dotnet run` restores the `MessagePack` NuGet package automatically on the
first run.  The client prints each received `DataMsg` and confirms the
`CommandMsg` it sends.

---

## Port and ID Configuration

All three components must agree on the following constants (defined in
`server/SystemIds.h` and mirrored in each client's source file):

| Symbol | Value | Direction |
|---|---|---|
| `DataMsgId` | 100 | Server → Client (DataMsg) |
| `CommandMsgId` | 101 | Client → Server (CommandMsg) |
| `DataRecvPort` | 8000 | Server PUBs / Client binds |
| `CmdSendPort` | 8001 | Client sends / Server SUBs |

---

## Message Definitions

Defined in `server/SystemMessages.h`; wire encoding via `MSGPACK_DEFINE`:

```cpp
struct Actuator {
    int    id;
    double position;
    double voltage;
    MSGPACK_DEFINE(id, position, voltage);   // wire: [id, position, voltage]
};

struct Sensor {
    int    id;
    double supplyVoltage;
    double reading;
    MSGPACK_DEFINE(id, supplyVoltage, reading); // wire: [id, supplyV, readingV]
};

struct DataMsg {
    std::vector<Actuator> actuators;
    std::vector<Sensor>   sensors;
    MSGPACK_DEFINE(actuators, sensors);      // wire: [[[...], ...], [[...], ...]]
};

struct CommandMsg {
    int pollingRateMs = 1000;
    MSGPACK_DEFINE(pollingRateMs);           // wire: [pollingRateMs]
};
```

---

## Protocol

Each UDP datagram starts with an 8-byte big-endian header (`DmqHeader`):

```
Offset  Size  Field
0       2     Marker   (always 0xAA55)
2       2     ID       (message type ID, e.g. 100 or 101)
4       2     SeqNum   (wraps at 65536)
6       2     Length   (payload length in bytes)
```

The header is immediately followed by `Length` bytes of MessagePack-encoded
payload.  After every non-ACK message the receiver sends a header-only ACK
(`ID=0`, `SeqNum` echoed, `Length=0`).

See `interop/README.md` for the full protocol specification.

---

## Interop Libraries

The reusable `DmqDataBus` libraries in `interop/` can be used in any project
that needs to communicate with a DelegateMQ DataBus application:

- `interop/python/` — `DmqDataBus`, `pack`/`unpack` helpers; see `interop/python/README.md`
- `interop/csharp/` — `DmqDataBus`, `Serializer`; see `interop/csharp/README.md`
