// Compiles to nothing unless DMQ_ALLOCATOR is selected, so every DelegateMQ source
// file can be added to a build (see src/DelegateMQ.cpp).
#include "delegate/DelegateSelect.h"
#if defined(DMQ_ALLOCATOR)

#include "Allocator.h"
#include "xallocator.h"
#include "delegate/DelegateOpt.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>


#ifndef CHAR_BIT
#define CHAR_BIT	8 
#endif

namespace dmq {

// @TODO: Uncomment while porting to check that each block's address is
// suitably aligned for the target (off by default)
//#define CHECK_ALIGNMENT

// Logic:
// 1. We store a 'BlockHeader' at the start of the raw block.
// 2. The user's memory immediately follows this header.
// 3. To support types like 'long double' or SSE/AVX vectors, the user's memory 
//    must often be aligned to 16 bytes (on 64-bit) or 8 bytes (on 32-bit).
//
// Calculation:
// - Safeguards ON: BLOCK_HEADER_SIZE is fixed at 16 bytes to provide a 
//   robust guard area and maintain 16-byte alignment for the user.
// - Safeguards OFF: 
//   - 64-bit System: Ptrs are 8 bytes. Next aligned boundary is 16. Set to 16.
//   - 32-bit System: Ptrs are 4 bytes. Next aligned boundary is 8. Set to 8.
// Single source of truth lives in xallocator.h; typed aliases for internal use.
static const size_t BLOCK_HEADER_SIZE = XALLOC_BLOCK_HEADER_SIZE;
static const size_t BLOCK_FOOTER_SIZE = XALLOC_BLOCK_FOOTER_SIZE;

struct BlockHeader {
	Allocator* allocator;
#ifdef DMQ_ALLOCATOR_SAFEGUARDS
	uint32_t magic;
	uint32_t canary;
#endif
};

#if defined(DMQ_XALLOCATOR_STATIC_POOLS)
	// Static pools (DMQ_XALLOCATOR_STATIC_POOLS): one pool per block size, each
	// with a fixed number of blocks, DMQ_XALLOC_POOL_<size> (DelegateMQConfig.h),
	// in static memory. No heap at all: a block size without a pool, or a full
	// pool, faults. Size them with xalloc_print_pool_config() (see xallocator.h).
	template <size_t Size, size_t Count>
	struct StaticPool
	{
		alignas(std::max_align_t) char storage[sizeof(AllocatorPool<char[Size], Count>)];
		Allocator* Construct() { return new (storage) AllocatorPool<char[Size], Count>(); }
	};

	// A size with no blocks takes no memory
	template <size_t Size>
	struct StaticPool<Size, 0>
	{
		Allocator* Construct() { return nullptr; }
	};

	static StaticPool<16, DMQ_XALLOC_POOL_16> _pool16;
	static StaticPool<32, DMQ_XALLOC_POOL_32> _pool32;
	static StaticPool<64, DMQ_XALLOC_POOL_64> _pool64;
	static StaticPool<128, DMQ_XALLOC_POOL_128> _pool128;
	static StaticPool<256, DMQ_XALLOC_POOL_256> _pool256;
	static StaticPool<512, DMQ_XALLOC_POOL_512> _pool512;
	static StaticPool<1024, DMQ_XALLOC_POOL_1024> _pool1024;
	static StaticPool<2048, DMQ_XALLOC_POOL_2048> _pool2048;
	static StaticPool<4096, DMQ_XALLOC_POOL_4096> _pool4096;
	static StaticPool<8192, DMQ_XALLOC_POOL_8192> _pool8192;
	static StaticPool<16384, DMQ_XALLOC_POOL_16384> _pool16384;

	#define MAX_ALLOCATORS	11
	static Allocator* _allocators[MAX_ALLOCATORS];
	static bool _staticPoolsReady = false;

	/// Construct the pools in their static storage, once. Caller must hold get_mutex(),
	/// or be xalloc_init() (single-threaded static init).
	static void init_static_pools()
	{
		if (_staticPoolsReady)
			return;
		Allocator* const pools[MAX_ALLOCATORS] = {
			_pool16.Construct(),
			_pool32.Construct(),
			_pool64.Construct(),
			_pool128.Construct(),
			_pool256.Construct(),
			_pool512.Construct(),
			_pool1024.Construct(),
			_pool2048.Construct(),
			_pool4096.Construct(),
			_pool8192.Construct(),
			_pool16384.Construct()
		};
		int n = 0;
		for (Allocator* pool : pools)
		{
			if (pool)
				_allocators[n++] = pool;
		}
		_staticPoolsReady = true;
	}
#else
	#define MAX_ALLOCATORS  DMQ_XALLOCATOR_MAX_ALLOCATORS
	static Allocator* _allocators[MAX_ALLOCATORS];
#endif	// DMQ_XALLOCATOR_STATIC_POOLS
	static_assert(MAX_ALLOCATORS == dmq::XALLOC_MAX_POOLS, "dmq::XALLOC_MAX_POOLS (DelegateOpt.h) must match");

// For C++ applications, must define AUTOMATIC_XALLOCATOR_INIT_DESTROY to 
// correctly ensure allocators are initialized before any static user C++ 
// construtor/destructor executes which might call into the xallocator API. 
// This feature costs 1-byte of RAM per C++ translation unit. This feature 
// can be disabled only under the following circumstances:
// 
// 1) The xallocator is only used within C files. 
// 2) DMQ_XALLOCATOR_STATIC_POOLS is undefined and the application never exits main (e.g. 
// an embedded system). 
//
// In either of the two cases above, call xalloc_init() in main at startup, 
// and xalloc_destroy() before main exits. In all other situations
// XallocInitDestroy must be used to call xalloc_init() and xalloc_destroy().
#ifdef AUTOMATIC_XALLOCATOR_INIT_DESTROY
std::atomic<int32_t> XallocInitDestroy::refCount(0);
XallocInitDestroy::XallocInitDestroy() 
{ 
	// Track how many static instances of XallocInitDestroy are created
	if (refCount++ == 0)
		::xalloc_init();
}

XallocInitDestroy::~XallocInitDestroy()
{
	// Last static instance to have destructor called?
	if (--refCount == 0)
		::xalloc_destroy();
}
#endif	// AUTOMATIC_XALLOCATOR_INIT_DESTROY

/// Returns the next higher powers of two. For instance, pass in 12 and 
/// the value returned would be 16. 
/// @param[in] k - numeric value to compute the next higher power of two.
/// @return	The next higher power of two based on the input k. 
template <class T>
T nexthigher(T k) 
{
    k--;
    for (size_t i=1; i<sizeof(T)*CHAR_BIT; i<<=1)
        k |= (k >> i);
    return k+1;
}

static dmq::Mutex& get_mutex()
{
	// Intentional immortal singleton: constructed in static storage and never
	// destroyed. A Meyers static would be destroyed during the static shutdown
	// sequence; any static destructor that runs afterward and calls xalloc_*
	// would then use a destroyed mutex. Static storage rather than new: the
	// allocator itself must not need the heap (DMQ_XALLOCATOR_STATIC_POOLS).
	alignas(dmq::Mutex) static char storage[sizeof(dmq::Mutex)];
	static dmq::Mutex* _mutex = new (storage) dmq::Mutex();
	return *_mutex;
}

#ifdef CHECK_ALIGNMENT
static void check_alignment(void* ptr)
{
	// Convert pointer to integer to perform bitwise check
	uintptr_t address = reinterpret_cast<uintptr_t>(ptr);

	// Check if the address is a multiple of the pointer size. 
	// On 32-bit systems, we often only get 4-byte alignment from the heap.
	// On 64-bit systems, we expect 8 or 16-byte alignment.
	if ((address & (sizeof(void*) - 1)) != 0)
	{
		DMQ_ASSERT();
	}
}
#endif

/// Stored a pointer to the allocator instance within the block region. 
///	a pointer to the client's area within the block.
/// @param[in] block - a pointer to the raw memory block. 
///	@param[in] allocator - the allocator instance.
///	@param[in] size - the client requested size of the memory block.
/// @return	A pointer to the client's address within the raw memory block. 
static inline void *set_block_allocator(void* block, Allocator* allocator)
{
	BlockHeader* pHeader = static_cast<BlockHeader*>(block);
	pHeader->allocator = allocator;
#ifdef DMQ_ALLOCATOR_SAFEGUARDS
	pHeader->magic = XALLOC_MAGIC;
	pHeader->canary = XALLOC_CANARY;
	size_t userSize = allocator->GetBlockSize() - BLOCK_HEADER_SIZE - BLOCK_FOOTER_SIZE;
	uint32_t* pFooter = (uint32_t*)((char*)block + BLOCK_HEADER_SIZE + userSize);
	*pFooter = XALLOC_CANARY;
#endif
	// Advance by BLOCK_HEADER_SIZE bytes (cast to char* first to do byte math)
	return (char*)block + BLOCK_HEADER_SIZE;
}

/// Gets the size of the memory block stored within the block.
/// @param[in] block - a pointer to the client's memory block. 
/// @return	The original allocator instance stored in the memory block.
static inline Allocator* get_block_allocator(void* block)
{
	BlockHeader* pHeader = (BlockHeader*)((char*)block - BLOCK_HEADER_SIZE);
#ifdef DMQ_ALLOCATOR_SAFEGUARDS
	DMQ_ASSERT_TRUE(pHeader->magic != XALLOC_FREED);
	DMQ_ASSERT_TRUE(pHeader->magic == XALLOC_MAGIC);
	DMQ_ASSERT_TRUE(pHeader->canary == XALLOC_CANARY);
	size_t userSize = pHeader->allocator->GetBlockSize() - BLOCK_HEADER_SIZE - BLOCK_FOOTER_SIZE;
	uint32_t* pFooter = (uint32_t*)((char*)pHeader + BLOCK_HEADER_SIZE + userSize);
	DMQ_ASSERT_TRUE(*pFooter == XALLOC_CANARY);
#endif
	return pHeader->allocator;
}

/// Returns the raw memory block pointer given a client memory pointer. 
/// @param[in] block - a pointer to the client memory block. 
/// @return	A pointer to the original raw memory block address. 
static inline void *get_block_ptr(void* block)
{
	return (char*)block - BLOCK_HEADER_SIZE;
}

/// Returns an allocator instance matching the size provided
/// @param[in] size - allocator block size
/// @return Allocator instance handling requested block size or NULL
/// if no allocator exists. 
static inline Allocator* find_allocator(size_t size)
{
	for (int i=0; i<MAX_ALLOCATORS; i++)
	{
		if (_allocators[i] == 0)
			break;
		
		if (_allocators[i]->GetBlockSize() == size)
			return _allocators[i];
	}
	
	return NULL;
}

/// Insert an allocator into the first empty slot. Caller must hold get_mutex().
/// @param[in] allocator - allocator instance to insert
/// @return true if inserted, false if the array is full
static inline bool insert_allocator(Allocator* allocator)
{
	for (int i = 0; i < MAX_ALLOCATORS; i++)
	{
		if (_allocators[i] == 0)
		{
			_allocators[i] = allocator;
			return true;
		}
	}
	return false;
}

} // namespace dmq

/// This function must be called exactly one time *before* any other xallocator
/// API is called. XallocInitDestroy constructor calls this function automatically. 
extern "C" void xalloc_init()
{
#if defined(DMQ_XALLOCATOR_STATIC_POOLS)
	// Construct the pools before any other static constructor can allocate.
	// xallocator_get_allocator() also does, under the lock, if called first.
	// No lock here: xalloc_init() runs single-threaded during static init,
	// before main(), where an RTOS mutex can't be created yet (CMSIS-RTOS2
	// before osKernelInitialize(), ThreadX before tx_kernel_enter()).
	dmq::init_static_pools();
#endif
}

/// Called one time when the application exits to cleanup any allocated memory.
/// ~XallocInitDestroy destructor calls this function automatically. 
extern "C" void xalloc_destroy()
{
	dmq::get_mutex().lock();

#if defined(DMQ_XALLOCATOR_STATIC_POOLS)
	for (int i=0; i<MAX_ALLOCATORS; i++)
	{
		if (dmq::_allocators[i])
			dmq::_allocators[i]->~Allocator();
		dmq::_allocators[i] = 0;
	}
	dmq::_staticPoolsReady = false;
#else
	for (int i=0; i<MAX_ALLOCATORS; i++)
	{
		if (dmq::_allocators[i] == 0)
			break;
		delete dmq::_allocators[i];
		dmq::_allocators[i] = 0;
	}
#endif

	dmq::get_mutex().unlock();
}

/// Get an Allocator instance based upon the client's requested block size.
/// If a Allocator instance is not currently available to handle the size,
///	then a new Allocator instance is create.
///	@param[in] size - the client's requested block size.
///	@return An Allocator instance that handles blocks of the requested
///	size, or NULL if size is too large to round up to a power of two or no
///	memory is available for a new Allocator.
extern "C" dmq::Allocator* xallocator_get_allocator(size_t size)
{
	// Too large to round up to a power of two without overflowing size_t:
	// nexthigher() would wrap to 0 and hand back a block smaller than size
	const size_t maxBlockSize = (static_cast<size_t>(-1) >> 1) + 1;
	if (size > maxBlockSize - dmq::BLOCK_HEADER_SIZE - dmq::BLOCK_FOOTER_SIZE)
		return NULL;

	// Based on the size, find the next higher powers of two value.
	size_t blockSize = size + dmq::BLOCK_HEADER_SIZE + dmq::BLOCK_FOOTER_SIZE;
	blockSize = dmq::nexthigher<size_t>(blockSize);

	dmq::get_mutex().lock();
#if defined(DMQ_XALLOCATOR_STATIC_POOLS)
	dmq::init_static_pools();
#endif
	dmq::Allocator* allocator = dmq::find_allocator(blockSize);
	dmq::get_mutex().unlock();

#if defined(DMQ_XALLOCATOR_STATIC_POOLS)
	// NULL: no static pool for this block size. The caller reports it with
	// BAD_ALLOC(). Add DMQ_XALLOC_POOL_<blockSize> to DelegateMQConfig.h;
	// xalloc_print_pool_config() lists the sizes used.
#else
	// If there is not an allocator already created to handle this block size
	if (allocator == NULL)  
	{
		// Note: Creating the Allocator object is done OUTSIDE the lock to avoid
		// AB/BA deadlocks with the system standard library locks.
		// nothrow: an exception must not leave this extern "C" function
		allocator = new (std::nothrow) dmq::Allocator(blockSize, 0, 0, "xallocator");
		if (allocator == NULL)
			return NULL;

		// Re-check and insert atomically under the same lock — eliminates the TOCTOU
		// window where two concurrent threads could each insert an Allocator for
		// the same block size, yielding duplicate entries and a use-after-free at shutdown.
		dmq::get_mutex().lock();
		dmq::Allocator* existing = dmq::find_allocator(blockSize);
		if (!existing)
		{
			if (!dmq::insert_allocator(allocator))
			{
				dmq::get_mutex().unlock();
				delete allocator;
				DMQ_ASSERT();
			}
			existing = allocator;
		}
		dmq::get_mutex().unlock();

		if (existing != allocator)
			delete allocator;
		allocator = existing;
	}
#endif
	
	return allocator;
}

/// Allocates a memory block of the requested size. The blocks are created from
///	the fixed block allocators.
///	@param[in] size - the client requested size of the block.
/// @return	A pointer to the client's memory block.
extern "C" void *xmalloc(size_t size)
{
	// 1. Get the bucket (surgical global locking inside). NULL: size too large.
	dmq::Allocator* allocator = xallocator_get_allocator(size);
	if (allocator == NULL)
		return NULL;
	void* blockMemoryPtr = NULL;

#if defined(DMQ_XALLOCATOR_STATIC_POOLS)
	// Static pools: a free block or an unused pool block; NULL when the pool is
	// full, which the caller reports with BAD_ALLOC(). Never the heap.
	dmq::get_mutex().lock();
	blockMemoryPtr = allocator->TryAllocate();
	dmq::get_mutex().unlock();
#else
	// 2. Fast Path: pop a recycled block from the free-list under the lock.
	dmq::get_mutex().lock();
	blockMemoryPtr = allocator->Pop();
	if (blockMemoryPtr != NULL)
		allocator->AccountAlloc(false);
	dmq::get_mutex().unlock();

	if (blockMemoryPtr == NULL)
	{
		// 3. Slow Path: fetch from heap OUTSIDE the lock to prevent AB/BA deadlocks
		// with standard library internal locks (e.g., MSVC _Lockit in debug builds).
		// nothrow: an exception must not leave this extern "C" function (see
		// CLAUDE.md). Callers (XALLOCATOR, stl_allocator) report NULL with BAD_ALLOC().
		blockMemoryPtr = (void*)new (std::nothrow) char[allocator->GetBlockSize()];
		if (blockMemoryPtr != NULL)
		{
			dmq::get_mutex().lock();
			allocator->AccountAlloc(true);
			dmq::get_mutex().unlock();
		}
	}
#endif

	if (blockMemoryPtr == NULL)
		return NULL;

	// Set the block Allocator* within the raw memory block region
	void* userPtr = dmq::set_block_allocator(blockMemoryPtr, allocator);

#ifdef CHECK_ALIGNMENT
	// Verify memory alignment before returning to the user
	dmq::check_alignment(blockMemoryPtr);
	dmq::check_alignment(userPtr);
#endif

	return userPtr;
}

/// Frees a memory block previously allocated with xalloc. The blocks are returned
///	to the fixed block allocator that originally created it.
///	@param[in] ptr - a pointer to a block created with xalloc.
extern "C" void xfree(void* ptr)
{
	if (ptr == 0)
		return;

	// Extract the original allocator instance from the caller's block pointer
	dmq::Allocator* allocator = dmq::get_block_allocator(ptr);

	// Convert the client pointer into the original raw block pointer
	void* blockPtr = dmq::get_block_ptr(ptr);

#ifdef DMQ_ALLOCATOR_SAFEGUARDS
	dmq::BlockHeader* pHeader = (dmq::BlockHeader*)blockPtr;
	pHeader->magic = XALLOC_FREED;
#endif

	dmq::get_mutex().lock();
	allocator->Deallocate(blockPtr);
	dmq::get_mutex().unlock();
}

/// Reallocates a memory block previously allocated with xalloc.
///	@param[in] ptr - a pointer to a block created with xalloc.
///	@param[in] size - the client requested block size to create.
extern "C" void *xrealloc(void *oldMem, size_t size)
{
	if (oldMem == 0)
		return xmalloc(size);

	if (size == 0) 
	{
		xfree(oldMem);
		return 0;
	}
	else 
	{
		// Create a new memory block
		void* newMem = xmalloc(size);
		if (newMem != 0) 
		{
			// Get the original allocator instance from the old memory block
			dmq::Allocator* oldAllocator = dmq::get_block_allocator(oldMem);
			size_t oldSize = oldAllocator->GetBlockSize() - dmq::BLOCK_HEADER_SIZE - dmq::BLOCK_FOOTER_SIZE;

			// Copy the bytes from the old memory block into the new (as much as will fit)
			memcpy(newMem, oldMem, (oldSize < size) ? oldSize : size);

			// Free the old memory block
			xfree(oldMem);

			// Return the client pointer to the new memory block
			return newMem;
		}
		return 0;
	}
}

/// Print, for each block size used, the most blocks in use at once so far, as
/// DelegateMQConfig.h lines for static pools. See xallocator.h.
extern "C" void xalloc_print_pool_config()
{
	dmq::get_mutex().lock();
	printf("// xallocator: peak blocks in use per block size, measured. Add headroom.\n");
	printf("#define DMQ_XALLOCATOR_STATIC_POOLS\n");
	// Smallest block size first (heap-blocks mode creates them in first-use order)
	size_t largest = 0;
	for (int i=0; i<MAX_ALLOCATORS && dmq::_allocators[i]; i++)
	{
		if (dmq::_allocators[i]->GetBlockSize() > largest)
			largest = dmq::_allocators[i]->GetBlockSize();
	}
	for (size_t size = 1; size != 0 && size <= largest; size <<= 1)
	{
		dmq::Allocator* allocator = dmq::find_allocator(size);
		if (allocator)
		{
			printf("#define DMQ_XALLOC_POOL_%-6u %u\n",
				static_cast<unsigned>(size), static_cast<unsigned>(allocator->GetBlockCount()));
		}
	}
	dmq::get_mutex().unlock();
}

/// Usage of each block size, smallest first (see xallocator.h)
extern "C" size_t xalloc_get_pool_stats(XallocPoolStats* stats, size_t maxPools)
{
	if (stats == NULL || maxPools == 0)
		return 0;
	dmq::get_mutex().lock();
#if defined(DMQ_XALLOCATOR_STATIC_POOLS)
	// As xallocator_get_allocator() does: report the pools even before the
	// first allocation, or after xalloc_destroy(), in a manual-init build
	dmq::init_static_pools();
#endif
	size_t largest = 0;
	for (int i=0; i<MAX_ALLOCATORS && dmq::_allocators[i]; i++)
	{
		if (dmq::_allocators[i]->GetBlockSize() > largest)
			largest = dmq::_allocators[i]->GetBlockSize();
	}
	size_t n = 0;
	// By size, as xalloc_print_pool_config() does (heap-blocks mode creates
	// allocators in first-use order)
	for (size_t size = 1; size != 0 && size <= largest && n < maxPools; size <<= 1)
	{
		dmq::Allocator* allocator = dmq::find_allocator(size);
		if (allocator == NULL)
			continue;
		// A block over 4 GB (only a direct xmalloc() can ask for one) reads as UINT32_MAX
		const size_t blockSize = allocator->GetBlockSize();
		stats[n].blockSize = static_cast<uint64_t>(blockSize) > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(blockSize);
		stats[n].blocksInUse = allocator->GetBlocksInUse();
		stats[n].peakBlocks = allocator->GetBlockCount();
		stats[n].capacity = allocator->GetMaxBlocks();
		n++;
	}
	dmq::get_mutex().unlock();
	return n;
}

/// Output xallocator usage statistics
extern "C" void xalloc_stats()
{
	dmq::get_mutex().lock();

	for (int i=0; i<MAX_ALLOCATORS; i++)
	{
		if (dmq::_allocators[i] == 0)
			break;

		const char* name = dmq::_allocators[i]->GetName();
		printf("%s Block Size: %u Block Count: %u Blocks In Use: %u\n",
			name != NULL ? name : "",
			static_cast<unsigned>(dmq::_allocators[i]->GetBlockSize()),
			static_cast<unsigned>(dmq::_allocators[i]->GetBlockCount()),
			static_cast<unsigned>(dmq::_allocators[i]->GetBlocksInUse()));
	}

	dmq::get_mutex().unlock();
}

#endif // DMQ_ALLOCATOR
