# Serialization Layer

This directory contains the serialization adapters for **DelegateMQ**, responsible for marshaling function arguments into binary or text formats for remote transmission.

DelegateMQ is designed to be **serialization-agnostic**. You can choose the serializer that best fits your project's performance, size, or compatibility requirements.

## Supported Serializers

The following subdirectories contain adapters for popular C++ serialization libraries:

* **`serialize`**: The built-in, header-only **MessageSerialize** library. 
    * *Best for:* Zero dependencies, simple projects, and built-in STL container support.
    * *Features:* Endianness handling, versioning, and no external requirements.
* **`msgpack`**: Adapter for **MessagePack** (msgpack-c).
    * *Best for:* High-performance binary serialization and compact data size.
* **`rapidjson`**: Adapter for **RapidJSON**.
    * *Best for:* Human-readable JSON output and web API integration.
* **`cereal`**: Adapter for **Cereal**.
    * *Best for:* Modern C++11/17 features and robust object serialization.
* **`bitsery`**: Adapter for **Bitsery**.
    * *Best for:* Ultra-fast, zero-buffer serialization for real-time applications.

Alternatively, you can implement your own adapter by inheriting from the `dmq::ISerializer` interface and injecting it into your `dmq::DelegateRemote` instance.

## Embedded Tradeoffs

Choosing a serializer for an embedded target involves constraints that do not apply on Linux or Windows: limited heap, no or restricted exception support, code-size budgets, and endianness differences between the MCU and any host it communicates with.

| Serializer | Embedded suitability | Notes |
|---|---|---|
| `serialize` | Good | Zero external dependencies, automatic endianness handling, optional no-exception path (`#ifdef __cpp_exceptions`). Structs must inherit `serialize::I` and implement `read()`/`write()`. Slightly larger wire format due to size-prefix versioning. |
| `bitsery` | Best for tight constraints | Header-only, designed for real-time/embedded use, produces the smallest payloads of all supported options. Plain structs work with a `serialize()` annotation — no base class required. Endianness is configured at the adapter level rather than handled automatically, so cross-architecture communication requires explicit configuration. |
| `msgpack` | Linux/Windows only | The msgpack-c library allocates dynamically (`msgpack::sbuffer`, `std::vector`) and carries an external dependency. These characteristics are manageable on a host but are a significant concern on a MCU with a constrained FreeRTOS heap. |
| `cereal` | Poor | Heavy template machinery and reliance on `<exception>` and `<memory>` increases code size considerably. Rarely justified on a Cortex-M or similar resource-constrained target. |
| `rapidjson` | Poor | JSON payload overhead (field names, brackets, quotes) is typically 3–10× larger than an equivalent binary format. Appropriate when a host-side consumer needs human-readable output or REST interoperability, not for high-rate sensor data loops on an MCU. |

### Guidance

- **Default embedded choice**: `serialize` — works out of the box with no external dependencies and handles the endianness problem automatically, which matters when an MCU (commonly big-endian capable) communicates with an x86 host.
- **Upgrade path**: `bitsery` — if wire payload size or serialization throughput is a priority on real hardware, bitsery is the natural next step. Only the struct annotations in the message definitions change; `dmq::databus::DataBus` API calls are unaffected.
- **Host-side only**: `msgpack`, `cereal`, and `rapidjson` are appropriate for Linux/Windows nodes in a mixed system but should not be the embedded node's serializer on a resource-constrained target.
