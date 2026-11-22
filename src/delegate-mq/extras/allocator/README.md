# Allocator Suite

A robust, fixed-block memory allocator suite for C++ applications, designed for performance and deterministic behavior in resource-constrained or real-time systems.

## Features

- **Fixed-Block Allocation**: Prevents heap fragmentation by using pre-allocated pools of equal-sized blocks.
- **C++ Class Integration**: `DECLARE_ALLOCATOR` and `IMPLEMENT_ALLOCATOR` macros provide easy integration with custom classes.
- **XALLOCATOR Macro**: Overloads `operator new` and `operator delete` for seamless use.
- **STL Compatibility**: `stl_allocator<T>` allows STL containers (`std::vector`, `std::list`, `std::map`, etc.) to use the fixed-block pool.
- **Smart Pointer Support**: `xmake_shared<T>` creates `std::shared_ptr` with both the object and control block allocated from the pool.
- **C API**: `xmalloc`, `xfree`, and `xrealloc` functions for C-style memory management.

## Components

- **Allocator**: The core engine for fixed-block management.
- **AllocatorPool**: A template-based pool for specific types.
- **stl_allocator**: STL-compliant allocator wrapper.
- **xmake_shared**: Helper for pool-allocated shared pointers.
- **xnew / xdelete**: Helper templates for placement new/destroy in the pool.

## Basic Usage

### Using XALLOCATOR in a class

```cpp
class MyClass {
    XALLOCATOR
public:
    int data;
};

// Now 'new MyClass' uses the fixed-block allocator
MyClass* obj = new MyClass();
delete obj;
```

### Using STL containers

```cpp
#include "xlist.h"

// xlist is a typedef for std::list using stl_allocator
xlist<int> myList;
myList.push_back(10);
```

### Using xmake_shared

```cpp
auto sp = xmake_shared<MyClass>(args...);
```

## Memory Alignment

The `xallocator` implementation ensures that all allocated blocks are correctly aligned for the target architecture:

- **Power-of-Two Rounding**: All requested block sizes are rounded up to the next power of two (8, 16, 32, 64, etc.). This inherently satisfies the natural alignment requirements of all standard C++ types (`int`, `double`, `long long`, etc.) and modern SIMD/AVX vector instructions.
- **Header Alignment**: A `BLOCK_HEADER_SIZE` (16 bytes on 64-bit, 8 bytes on 32-bit) is added to each block to store metadata. Both the raw block and the user's data pointer remain aligned on 8 or 16-byte boundaries.
- **Efficiency vs. Safety**: This strategy prioritizes "automatic" alignment and safety over maximum memory density. While it may result in some internal fragmentation, it ensures that memory is always safe for any data type without manual alignment configuration.

## Static Pools (No Heap)

By default `xallocator` takes blocks from the heap as it needs them and recycles them, never returning them (heap-blocks mode). To use no heap at all, define `DMQ_XALLOCATOR_STATIC_POOLS` and the number of blocks of each size in `DelegateMQConfig.h`:

```cpp
#define DMQ_XALLOCATOR_STATIC_POOLS
#define DMQ_XALLOC_POOL_32     48      // blocks of 32 bytes, header included
#define DMQ_XALLOC_POOL_64     12
#define DMQ_XALLOC_POOL_128    4
```

Sizes are powers of two from 16 to 16384 (the request plus the block header, rounded up); a size left at 0 takes no memory. A full pool, or a size with no pool, makes `xmalloc()` return NULL, which `XALLOCATOR`, `stl_allocator` and `xmake_shared` report with `BAD_ALLOC()`. To size the pools, run in heap-blocks mode under the heaviest load and call `xalloc_print_pool_config()`, which prints these lines with the peak blocks in use; add headroom. See "No Heap After Startup" in [docs/SAFETY.md](../../../../docs/SAFETY.md).

## Integration with DelegateMQ

To enable the allocator suite within DelegateMQ, define `DMQ_ALLOCATOR` in your build configuration. This will cause DelegateMQ's internal containers and remote delegate marshalling to use the fixed-block pools instead of the standard heap.
