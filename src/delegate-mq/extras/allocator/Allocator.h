#ifndef __ALLOCATOR_H
#define __ALLOCATOR_H

#include "../../delegate/DelegateOpt.h"
#include <cstdint>
#include <cstddef>
#include <stddef.h>

namespace dmq {

/// @see https://github.com/endurodave/Allocator
/// David Lafreniere
class Allocator
{
public:
    /// Constructor
    /// @param[in]  size - size of the fixed blocks
    /// @param[in]  objects - maximum number of object. If 0, new blocks are
	///		created off the heap as necessary.
	/// @param[in]	memory - pointer to a block of static memory for allocator or NULL 
	///		to obtain memory from global heap. If not NULL, the objects argument 
	///		defines the size of the memory block (size x objects = memory size in bytes).
	///	@param[in]	name - optional allocator name string.
    Allocator(size_t size, uint32_t objects=0, char* memory = NULL, const char* name=NULL);

    /// Destructor
    ~Allocator();

    /// Get a pointer to a memory block. 
    /// @param[in]  size - size of the block to allocate
    /// @return     Returns pointer to the block. Otherwise NULL if unsuccessful.
    void* Allocate(size_t size);

    /// Return a pointer to the memory pool.
    /// @param[in]  pBlock - block of memory deallocate (i.e push onto free-list)
    void Deallocate(void* pBlock);

    /// Update allocation stats when xmalloc bypasses Allocate() to avoid holding
    /// the global lock during heap allocation. Must be called under the global lock.
    /// @param[in] newBlock - true if a fresh block was obtained from the heap.
    void AccountAlloc(bool newBlock);

    /// Push a memory block onto head of free-list.
    /// @param[in]  pMemory - block of memory to push onto free-list
    void Push(void* pMemory);

    /// Pop a memory block from head of free-list.
    /// @return     Returns pointer to the block. Otherwise NULL if unsuccessful.
    void* Pop();

    /// Get the allocator name string.
    /// @return		A pointer to the allocator name or NULL if none was assigned.
    const char* GetName() { return m_name; }

    /// Gets the fixed block memory size, in bytes, handled by the allocator.
    /// @return		The fixed block size in bytes.
    size_t GetBlockSize() const { return m_blockSize; }

    /// Gets the maximum number of blocks created by the allocator.
    /// @return		The number of fixed memory blocks created.
    uint32_t GetBlockCount() { return m_blockCnt; }

    /// Gets the number of blocks in use.
    /// @return		The number of fixed memory blocks in use.
    uint32_t GetBlocksInUse() { return m_blocksInUse; }

    /// Gets the total number of allocations for this allocator instance.
    /// @return		The total number of allocations.
    uint32_t GetAllocations() { return m_allocations; }

    /// Gets the total number of deallocations for this allocator instance.
    /// @return		The total number of deallocations.
    uint32_t GetDeallocations() { return m_deallocations; }
	
private:
    struct Block
    {
        Block* pNext;
    };

	enum AllocatorMode { HEAP_BLOCKS, HEAP_POOL, STATIC_POOL };

    const size_t m_blockSize;
    const size_t m_objectSize;
    const uint32_t m_maxObjects;
	AllocatorMode m_allocatorMode;
    Block* m_pHead;
    char* m_pPool;
    uint32_t m_poolIndex;
    uint32_t m_blockCnt;
    uint32_t m_blocksInUse;
    uint32_t m_allocations;
    uint32_t m_deallocations;
    const char* m_name;
};

// Template class to create external memory pool
template <class T, uint32_t Objects>
class AllocatorPool : public Allocator
{
public:
	AllocatorPool() : Allocator(sizeof(T), Objects, m_memory)
	{
	}
private:
	alignas(std::max_align_t) char m_memory[sizeof(T) * Objects];
};

} // namespace dmq

// macro to provide header file interface
#define DECLARE_ALLOCATOR \
    public: \
        void* operator new(size_t size) { \
            return _allocator.Allocate(size); \
        } \
        void operator delete(void* pObject) { \
            _allocator.Deallocate(pObject); \
        } \
    private: \
        static dmq::Allocator _allocator; 

// macro to provide source file interface
#define IMPLEMENT_ALLOCATOR(class, objects, memory) \
	dmq::Allocator class::_allocator(sizeof(class), objects, memory, #class);

#endif



