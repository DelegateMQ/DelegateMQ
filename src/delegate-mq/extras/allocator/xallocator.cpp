#include "Allocator.h"
#include "xallocator.h"
#include "delegate/DelegateOpt.h"
#include <cstring>
#include <iostream>
#include <mutex>
#include <new>

using namespace std;

#ifndef char_BIT
#define char_BIT	8 
#endif

// @TODO: Comment out to disable alignment check if desired after porting
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

// Define STATIC_POOLS to switch from heap blocks mode to static pools mode
//#define STATIC_POOLS 
#ifdef STATIC_POOLS
	// Update this section as necessary if you want to use static memory pools.
	// See also xalloc_init() and xalloc_destroy() for additional updates required.
	#define MAX_ALLOCATORS	10
	#define MAX_BLOCKS		32

	// Create static storage for each static allocator instance
	char* _allocator8 [sizeof(AllocatorPool<char[8], MAX_BLOCKS>)];
	char* _allocator16 [sizeof(AllocatorPool<char[16], MAX_BLOCKS>)];
	char* _allocator32 [sizeof(AllocatorPool<char[32], MAX_BLOCKS>)];
	char* _allocator64 [sizeof(AllocatorPool<char[64], MAX_BLOCKS>)];
	char* _allocator128 [sizeof(AllocatorPool<char[128], MAX_BLOCKS>)];
	char* _allocator256 [sizeof(AllocatorPool<char[256], MAX_BLOCKS>)];
	char* _allocator512 [sizeof(AllocatorPool<char[512], MAX_BLOCKS>)];
	char* _allocator1024 [sizeof(AllocatorPool<char[1024], MAX_BLOCKS>)];
	char* _allocator2048 [sizeof(AllocatorPool<char[2048], MAX_BLOCKS>)];	
	char* _allocator4096 [sizeof(AllocatorPool<char[4096], MAX_BLOCKS>)];

	// Array of pointers to all allocator instances
	static Allocator* _allocators[MAX_ALLOCATORS];

#else
	#define MAX_ALLOCATORS  DMQ_XALLOCATOR_MAX_ALLOCATORS
	static Allocator* _allocators[MAX_ALLOCATORS];
#endif	// STATIC_POOLS

// For C++ applications, must define AUTOMATIC_XALLOCATOR_INIT_DESTROY to 
// correctly ensure allocators are initialized before any static user C++ 
// construtor/destructor executes which might call into the xallocator API. 
// This feature costs 1-byte of RAM per C++ translation unit. This feature 
// can be disabled only under the following circumstances:
// 
// 1) The xallocator is only used within C files. 
// 2) STATIC_POOLS is undefined and the application never exits main (e.g. 
// an embedded system). 
//
// In either of the two cases above, call xalloc_init() in main at startup, 
// and xalloc_destroy() before main exits. In all other situations
// XallocInitDestroy must be used to call xalloc_init() and xalloc_destroy().
#ifdef AUTOMATIC_XALLOCATOR_INIT_DESTROY
int32_t XallocInitDestroy::refCount = 0;
XallocInitDestroy::XallocInitDestroy() 
{ 
	// Track how many static instances of XallocInitDestroy are created
	if (refCount++ == 0)
		xalloc_init();
}

XallocInitDestroy::~XallocInitDestroy()
{
	// Last static instance to have destructor called?
	if (--refCount == 0)
		xalloc_destroy();
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
    for (size_t i=1; i<sizeof(T)*char_BIT; i<<=1)
        k |= (k >> i);
    return k+1;
}

static dmq::Mutex& get_mutex()
{
	static dmq::Mutex _mutex;
	return _mutex;
}

#ifdef CHECK_ALIGNMENT
#include <cstddef>  // for max_align_t
#include <cstdio>   // for fprintf
#include <assert.h>

static void check_alignment(void* ptr)
{
	// Convert pointer to integer to perform bitwise check
	uintptr_t address = reinterpret_cast<uintptr_t>(ptr);

	// Check if the address is a multiple of the pointer size. 
	// On 32-bit systems, we often only get 4-byte alignment from the heap.
	// On 64-bit systems, we expect 8 or 16-byte alignment.
	if ((address & (sizeof(void*) - 1)) != 0)
	{
		assert(false && "xmalloc returning misaligned memory");
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
	ASSERT_TRUE(pHeader->magic != XALLOC_FREED);
	ASSERT_TRUE(pHeader->magic == XALLOC_MAGIC);
	ASSERT_TRUE(pHeader->canary == XALLOC_CANARY);
	size_t userSize = pHeader->allocator->GetBlockSize() - BLOCK_HEADER_SIZE - BLOCK_FOOTER_SIZE;
	uint32_t* pFooter = (uint32_t*)((char*)pHeader + BLOCK_HEADER_SIZE + userSize);
	ASSERT_TRUE(*pFooter == XALLOC_CANARY);
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

/// This function must be called exactly one time *before* any other xallocator
/// API is called. XallocInitDestroy constructor calls this function automatically. 
extern "C" void xalloc_init()
{
#ifdef STATIC_POOLS
	get_mutex().lock();

	// For STATIC_POOLS mode, the allocators must be initialized before any other
	// static user class constructor is run. Therefore, use placement new to initialize
	// each allocator into the previously reserved static memory locations.
	new (&_allocator8) AllocatorPool<char[8], MAX_BLOCKS>();
	new (&_allocator16) AllocatorPool<char[16], MAX_BLOCKS>();
	new (&_allocator32) AllocatorPool<char[32], MAX_BLOCKS>();
	new (&_allocator64) AllocatorPool<char[64], MAX_BLOCKS>();
	new (&_allocator128) AllocatorPool<char[128], MAX_BLOCKS>();
	new (&_allocator256) AllocatorPool<char[256], MAX_BLOCKS>();
	new (&_allocator512) AllocatorPool<char[512], MAX_BLOCKS>();
	new (&_allocator1024) AllocatorPool<char[1024], MAX_BLOCKS>();
	new (&_allocator2048) AllocatorPool<char[2048], MAX_BLOCKS>();
	new (&_allocator4096) AllocatorPool<char[4096], MAX_BLOCKS>();

	// Populate allocator array with all instances
	_allocators[0] = (Allocator*)&_allocator8;
	_allocators[1] = (Allocator*)&_allocator16;
	_allocators[2] = (Allocator*)&_allocator32;
	_allocators[3] = (Allocator*)&_allocator64;
	_allocators[4] = (Allocator*)&_allocator128;
	_allocators[5] = (Allocator*)&_allocator256;
	_allocators[6] = (Allocator*)&_allocator512;
	_allocators[7] = (Allocator*)&_allocator1024;
	_allocators[8] = (Allocator*)&_allocator2048;
	_allocators[9] = (Allocator*)&_allocator4096;

	get_mutex().unlock();
#endif
}

/// Called one time when the application exits to cleanup any allocated memory.
/// ~XallocInitDestroy destructor calls this function automatically. 
extern "C" void xalloc_destroy()
{
	get_mutex().lock();

#ifdef STATIC_POOLS
	for (int i=0; i<MAX_ALLOCATORS; i++)
	{
		if (_allocators[i])
			_allocators[i]->~Allocator();
		_allocators[i] = 0;
	}
#else
	for (int i=0; i<MAX_ALLOCATORS; i++)
	{
		if (_allocators[i] == 0)
			break;
		delete _allocators[i];
		_allocators[i] = 0;
	}
#endif

	get_mutex().unlock();
}

/// Get an Allocator instance based upon the client's requested block size.
/// If a Allocator instance is not currently available to handle the size,
///	then a new Allocator instance is create.
///	@param[in] size - the client's requested block size.
///	@return An Allocator instance that handles blocks of the requested
///	size.
extern "C" Allocator* xallocator_get_allocator(size_t size)
{
	// Based on the size, find the next higher powers of two value.
	size_t blockSize = size + BLOCK_HEADER_SIZE + BLOCK_FOOTER_SIZE;
	blockSize = nexthigher<size_t>(blockSize);

	get_mutex().lock();
	Allocator* allocator = find_allocator(blockSize);
	get_mutex().unlock();

#ifdef STATIC_POOLS
	ASSERT_TRUE(allocator != NULL);
#else
	// If there is not an allocator already created to handle this block size
	if (allocator == NULL)  
	{
		// Note: Creating the Allocator object is done OUTSIDE the lock to avoid
		// AB/BA deadlocks with the system standard library locks.
		allocator = new Allocator(blockSize, 0, 0, "xallocator");

		// Re-check and insert atomically under the same lock — eliminates the TOCTOU
		// window where two concurrent threads could each insert an Allocator for
		// the same block size, yielding duplicate entries and a use-after-free at shutdown.
		get_mutex().lock();
		Allocator* existing = find_allocator(blockSize);
		if (!existing)
		{
			if (!insert_allocator(allocator))
			{
				get_mutex().unlock();
				delete allocator;
				ASSERT();
				return NULL;
			}
			existing = allocator;
		}
		get_mutex().unlock();

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
	// 1. Get the bucket (surgical global locking inside)
	Allocator* allocator = xallocator_get_allocator(size);
	size_t blockSize = allocator->GetBlockSize();

	void* blockMemoryPtr = NULL;

#ifdef STATIC_POOLS
	// STATIC_POOLS: Allocate() handles pool slot distribution, exhaustion detection,
	// and stat tracking. Must be called under the lock.
	get_mutex().lock();
	blockMemoryPtr = allocator->Allocate(blockSize);
	get_mutex().unlock();
#else
	// 2. Fast Path: pop a recycled block from the free-list under the lock.
	get_mutex().lock();
	blockMemoryPtr = allocator->Pop();
	if (blockMemoryPtr != NULL)
		allocator->AccountAlloc(false);
	get_mutex().unlock();

	if (blockMemoryPtr == NULL)
	{
		// 3. Slow Path: fetch from heap OUTSIDE the lock to prevent AB/BA deadlocks
		// with standard library internal locks (e.g., MSVC _Lockit in debug builds).
		blockMemoryPtr = (void*)new char[blockSize];
		if (blockMemoryPtr != NULL)
		{
			get_mutex().lock();
			allocator->AccountAlloc(true);
			get_mutex().unlock();
		}
	}
#endif

	if (blockMemoryPtr == NULL)
		return NULL;

	// Set the block Allocator* within the raw memory block region
	void* userPtr = set_block_allocator(blockMemoryPtr, allocator);

#ifdef CHECK_ALIGNMENT
	// Verify memory alignment before returning to the user
	check_alignment(blockMemoryPtr);
	check_alignment(userPtr);
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
	Allocator* allocator = get_block_allocator(ptr);

	// Convert the client pointer into the original raw block pointer
	void* blockPtr = get_block_ptr(ptr);

#ifdef DMQ_ALLOCATOR_SAFEGUARDS
	BlockHeader* pHeader = (BlockHeader*)blockPtr;
	pHeader->magic = XALLOC_FREED;
#endif

	get_mutex().lock();
	allocator->Deallocate(blockPtr);
	get_mutex().unlock();
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
			Allocator* oldAllocator = get_block_allocator(oldMem);
			size_t oldSize = oldAllocator->GetBlockSize() - BLOCK_HEADER_SIZE - BLOCK_FOOTER_SIZE;

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

/// Output xallocator usage statistics
extern "C" void xalloc_stats()
{
	get_mutex().lock();

	for (int i=0; i<MAX_ALLOCATORS; i++)
	{
		if (_allocators[i] == 0)
			break;

		if (_allocators[i]->GetName() != NULL)
			cout << _allocators[i]->GetName();
		cout << " Block Size: " << _allocators[i]->GetBlockSize();
		cout << " Block Count: " << _allocators[i]->GetBlockCount();
		cout << " Blocks In Use: " << _allocators[i]->GetBlocksInUse();
		cout << endl;
	}

	get_mutex().unlock();
}


