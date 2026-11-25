# Serializers (DelegateMQ Abstraction Layer)

This directory contains the serializer part of the **DelegateMQ Abstraction Layer**: the `ISerializer` adapters responsible for marshaling function arguments into binary or text formats for remote transmission.

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
* **`raw`**: **`dmq::serialization::raw::Serializer`**, DelegateMQ's own raw-copy serializer: each argument's bytes are copied, with no encoding.
    * *Best for:* Peers that share an ABI: two cores of one chip over shared memory, two processes of one build, identical boards. High-rate data such as sample blocks.
    * *Always available:* no dependencies, so `DelegateMQ.h` includes it whichever `DMQ_SERIALIZE_*` is selected. Use it for some messages and another serializer for the rest (serializers are chosen per remote delegate and per DataBus topic).
    * *Limits:* trivially copyable arguments only (enforced at compile time), the same struct layout on both sides (not just the same endianness), no versioning. See [Raw Serializer](#raw-serializer).

Alternatively, you can implement your own adapter by inheriting from the `dmq::ISerializer` interface and injecting it into your `dmq::DelegateRemote` instance.

## Embedded Tradeoffs

Choosing a serializer for an embedded target involves constraints that do not apply on Linux or Windows: limited heap, no or restricted exception support, code-size budgets, and endianness differences between the MCU and any host it communicates with.

| Serializer | Embedded suitability | Notes |
|---|---|---|
| `serialize` | Good | Zero external dependencies, automatic endianness handling, builds without exceptions (errors are reported through the `ByteWriter`/`ByteReader` state). Structs must inherit `serialize::I` and implement `read()`/`write()`. Slightly larger wire format due to size-prefix versioning. |
| `bitsery` | Best for tight constraints | Header-only, designed for real-time/embedded use, produces the smallest payloads of all supported options. Plain structs work with a `serialize()` annotation — no base class required. Endianness is configured at the adapter level rather than handled automatically, so cross-architecture communication requires explicit configuration. |
| `msgpack` | Linux/Windows only | The msgpack-c library allocates dynamically (`msgpack::sbuffer`, `std::vector`) and carries an external dependency. These characteristics are manageable on a host but are a significant concern on a MCU with a constrained FreeRTOS heap. |
| `cereal` | Poor | Heavy template machinery and reliance on `<exception>` and `<memory>` increases code size considerably. Rarely justified on a Cortex-M or similar resource-constrained target. |
| `raw` | Best for same-ABI peers | No dependencies, no encoding: one copy per argument, so the fastest and the payload is exactly the arguments' sizes. Trivially copyable arguments only; both sides must share the struct layout (same compiler and architecture), so it's for two cores of one chip, not an MCU talking to a PC. |
| `rapidjson` | Poor | JSON payload overhead (field names, brackets, quotes) is typically 3–10× larger than an equivalent binary format. Appropriate when a host-side consumer needs human-readable output or REST interoperability, not for high-rate sensor data loops on an MCU. |

### Guidance

- **Default embedded choice**: `serialize` — works out of the box with no external dependencies and handles the endianness problem automatically, which matters when an MCU (commonly big-endian capable) communicates with an x86 host.
- **Upgrade path**: `bitsery` — if wire payload size or serialization throughput is a priority on real hardware, bitsery is the natural next step. Only the struct annotations in the message definitions change; `dmq::databus::DataBus` API calls are unaffected.
- **Host-side only**: `msgpack`, `cereal`, and `rapidjson` are appropriate for Linux/Windows nodes in a mixed system but should not be the embedded node's serializer on a resource-constrained target.
- **Message field types**: on the MCU, prefer `std::array`, `xlist`/`xmap`/`xset` and `xstring` over `std::vector`. See [Choosing Containers on Embedded Targets](serialize/README.md#choosing-containers-on-embedded-targets).
- **Same-ABI peers**: `raw` for high-rate messages between two cores of one chip (or two processes of one build), next to `serialize` for everything else.

## Raw Serializer

`port/serialize/raw/RawSerializer.h`: copies each argument's bytes. No type tags, no endianness marker, no size prefixes, no `serialize::I` base class.

```cpp
struct AdcBlock { uint32_t seq; std::array<int16_t, 512> samples; };

dmq::serialization::raw::Serializer<void(const AdcBlock&)> rawSer;    // like any serializer
dmq::databus::DataBus::RegisterSerializer<AdcBlock>("adc/block", rawSer);
```

- **Arguments**: every argument, or for a pointer argument the object it points to, must be trivially copyable: plain structs of fixed-width fields, enums, `std::array` of those, nested structs of those. `std::string`, `xstring`, containers, smart pointers, classes with virtual functions, `void*`, `T**` and `const char*` (which would copy one character) are compile errors. A pointer argument copies the one object it points to; a null pointer fails the write.
- **Same ABI, not just the same endianness**: both sides must lay the types out identically: padding, alignment and type sizes. Use fixed-width types (`int32_t`, not `long`, which is 4 bytes on Windows and ARM but 8 on 64-bit Linux), give enums a fixed underlying type, and build both sides from the same headers. A 32-bit and a 64-bit process, or MSVC and MinGW, can disagree.
- **No versioning**: a field added on one side only isn't tolerated. `Read()` fails unless exactly the expected number of bytes arrives (`MESSAGE_SIZE`), which catches most layout mismatches, though not two same-size fields swapped.
- **Pointer members**: a struct holding a pointer is trivially copyable, so it compiles, but the pointer means nothing on the other side (unless it points into memory shared at the same address). Don't send one.
- **Not zero-copy**: it removes the encoding, not the copies into and out of the transport.
