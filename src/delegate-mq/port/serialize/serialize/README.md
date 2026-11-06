# MessageSerialize

A robust, header-only C++ binary serialization library designed for DelegateMQ. It provides a simple yet powerful framework for marshaling C++ objects, primitives, and STL containers into binary streams.

## Features

- **Header-Only**: No external dependencies beyond the C++ Standard Library.
- **Endianness Aware**: Uses a little-endian wire format. Automatically handles byte-swapping on big-endian hosts.
- **STL Support**: Native support for `std::vector`, `std::array`, `std::list`, `std::map`, `std::set`, `std::string`, and `std::wstring`, including nested containers (e.g. `std::vector<std::vector<int>>`, `std::map<std::string, std::list<int>>`).
- **Custom Allocators**: Fully compatible with custom allocators, including DelegateMQ's `xstring`, `xlist`, `xmap` and `xset`. Declare message fields with these types and `DMQ_ALLOCATOR` switches them between the fixed-block allocator and the heap, with no serializer changes.
- **Versioning**: Supports forward and backward compatibility for user-defined objects.
- **Type Safety**: Uses compile-time checks to ensure only supported types are serialized.
- **Pointer Support**: Supports serializing containers of pointers with automatic memory allocation during deserialization.
- **Stream Verification**: Includes runtime and compile-time checks (via `is_seekable` trait) to ensure streams support the required seeking operations.

## Stream Requirements

- **Seekable**: The library requires seekable streams (e.g., `std::stringstream`, `std::fstream`) to handle object versioning and size backfilling. Non-seekable streams (like sockets or `std::cout`) will trigger a runtime error.
- **Binary Mode**: Always open files or streams in **binary mode** (`std::ios::binary`). Failure to do so may lead to data corruption due to automatic newline translations (especially on Windows).

## Limits

Set in `DelegateMQConfig.h` (defaults in `DelegateMQConfig_Default.h`). They apply on write and on read, so a received message can't make the receiver allocate more than this. Exceeding one fails the stream with the error shown.

| Setting | Default | Limits | Error |
|---|---|---|---|
| `DMQ_SERIALIZE_MAX_STRING_SIZE` | 1024 | String length in bytes, not counting a terminator (none is sent); at most 65535 | `STRING_TOO_LONG` |
| `DMQ_SERIALIZE_MAX_CONTAINER_SIZE` | 64 | Elements in a vector, list, set or map; at most 65535 | `CONTAINER_TOO_MANY` |
| `DMQ_SERIALIZE_MAX_NESTING` | 5 | Depth of `serialize::I` objects nested inside each other | `NESTING_TOO_DEEP` |

## Basic Usage

### Serializing Primitives

```cpp
#include "msg_serialize.h"
#include <sstream>

serialize ms;
int value = 123;
std::stringstream ss;

// Write to stream
ms.write(ss, value);

// Read from stream
int outValue;
ms.read(ss, outValue);
```

### Custom Serializable Objects

User-defined types must inherit from `serialize::I` and implement the `read()` and `write()` methods.

```cpp
struct MyData : public serialize::I {
    int id = 0;
    std::string name;

    // Serialize object to stream
    std::ostream& write(serialize& ms, std::ostream& os) const override {
        ms.write(os, id);
        ms.write(os, name);
        return os;
    }

    // Deserialize object from stream
    std::istream& read(serialize& ms, std::istream& is) override {
        ms.read(is, id);
        ms.read(is, name);
        return is;
    }
};
```

### STL Containers and Custom Allocators

The library handles standard containers and those using custom allocators (like fixed-block pools) automatically.

```cpp
xlist<int> myList = {1, 2, 3};
std::stringstream ss;
serialize ms;

ms.write(ss, myList);

xlist<int> outList;
ms.read(ss, outList);
```

### Choosing Containers on Embedded Targets

What a message type's fields allocate matters more on an MCU than how they're serialized. With `DMQ_ALLOCATOR`, memory comes from fixed-block pools, one pool per power-of-two block size, and blocks are kept for reuse rather than returned to the heap.

| Field type | Allocation | Embedded use |
|---|---|---|
| `std::array<T, N>`, `char[N]` | None | **Best.** Fixed size, no heap and no pool. |
| `xlist`, `xmap`, `xset` | One same-size node per element, from one fixed-block pool | **Good.** Freed nodes are reused exactly. |
| `xstring` | One block sized to the string, from a fixed-block pool | **Good**, bounded by `DMQ_SERIALIZE_MAX_STRING_SIZE`. |
| `std::vector` | One contiguous block that is reallocated larger as it grows | **Host-side only.** Each growth step needs a new, larger block size, so a vector touches several pools and each keeps its blocks. In static-pool mode the largest block is 4096 bytes. |

- On the MCU, declare message fields with `std::array`, `xlist`/`xmap`/`xset` and `xstring`. On desktop or gateway nodes, `std::vector` is fine.
- `std::array<T, N>` deliberately uses the vector type marker rather than a separate array type, so its bytes match a vector's (marker, count, elements) and one node can use `std::vector<T>` while the other uses `std::array<T, N>` for the same field. The count on the wire catches a mismatch: reading into a `std::array` fails with `INVALID_INPUT` unless exactly `N` elements arrive, and a list, set or map fails with `TYPE_MISMATCH`. As with vectors, the marker identifies the container kind, not the element type.
- Reading into a `std::vector` reserves the received count first, so it makes one allocation of the final size rather than regrowing per element.

## Advanced Features

### Versioning & Compatibility

When serializing objects derived from `serialize::I`, the library records the object size in the stream. 
- **Forward Compatibility**: If a receiver gets a newer (larger) version of an object, it reads what it knows and safely skips the remaining bytes.
- **Backward Compatibility**: If a receiver gets an older (smaller) version, it reads the available data and leaves the remaining members at their default values.

### Pointer Containers

When serializing `std::vector<T*>` or `std::list<T*>`, the library:
1.  Writes a boolean flag for each element indicating if it's null.
2.  For non-null elements, it serializes the pointed-to object.
3.  During deserialization, it calls `new T` to recreate the objects. 

*Note: The caller is responsible for managing the memory of objects created during pointer container deserialization.*
