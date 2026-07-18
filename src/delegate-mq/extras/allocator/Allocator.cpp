#include "Allocator.h"
#include "delegate/DelegateOpt.h"
#include <new>

namespace dmq {

//------------------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------------------
Allocator::Allocator(size_t size, uint32_t objects, char* memory, const char* name) :
    m_blockSize(size < sizeof(long*) ? sizeof(long*):size),
    m_objectSize(size),
    m_maxObjects(objects),
    m_pHead(NULL),
    m_poolIndex(0),
    m_blockCnt(0),
    m_blocksInUse(0),
    m_allocations(0),
    m_deallocations(0),
    m_name(name)
{
    // If using a fixed memory pool 
	if (m_maxObjects)
	{
		// If caller provided an external memory pool
		if (memory)
		{
			m_pPool = memory;
			m_allocatorMode = STATIC_POOL;
		}
		else 
		{
			m_pPool = (char*)new char[m_blockSize * m_maxObjects];
			m_allocatorMode = HEAP_POOL;
		}
	}
	else
	{
		m_pPool = NULL;
		m_allocatorMode = HEAP_BLOCKS;
	}
}

//------------------------------------------------------------------------------
// Destructor
//------------------------------------------------------------------------------
Allocator::~Allocator()
{
	// If using pool then destroy it, otherwise traverse free-list and 
	// destroy each individual block
	if (m_allocatorMode == HEAP_POOL)
		delete [] m_pPool;
	else if (m_allocatorMode == HEAP_BLOCKS)
	{
		while(m_pHead)
			delete [] (char*)Pop();
	}
}

//------------------------------------------------------------------------------
// Allocate
//------------------------------------------------------------------------------
void* Allocator::Allocate([[maybe_unused]] size_t size)
{
    ASSERT_TRUE(size <= m_objectSize);
	
    // If can't obtain existing block then get a new one
    void* pBlock = Pop();
    if (!pBlock)
    {
        // If using a pool method then get block from pool,
        // otherwise using dynamic so get block from heap
        if (m_maxObjects)
        {
            // If we have not exceeded the pool maximum
            if(m_poolIndex < m_maxObjects)
            {
                pBlock = (void*)(m_pPool + (m_poolIndex++ * m_blockSize));
            }
            else
            {
                // Get the pointer to the new handler
                std::new_handler handler = std::set_new_handler(0);
                std::set_new_handler(handler);

                while (!pBlock)
                {
                    if (handler)
                    {
                        (*handler)();
                        pBlock = Pop();
                    }
                    else
                    {
                        BAD_ALLOC();
                    }
                }
            }
        }
        else
        {
        	m_blockCnt++;
            pBlock = (void*)new char[m_blockSize];
        }
    }

    if (pBlock)
    {
        m_blocksInUse++;
        m_allocations++;
    }
	
    return pBlock;
}

//------------------------------------------------------------------------------
// Deallocate
//------------------------------------------------------------------------------
void Allocator::Deallocate(void* pBlock)
{
#ifdef DMQ_ALLOCATOR_SAFEGUARDS
	if (m_allocatorMode == STATIC_POOL || m_allocatorMode == HEAP_POOL)
	{
		// Check that pBlock is within the pool range
		char* pCharBlock = (char*)pBlock;
		ASSERT_TRUE(pCharBlock >= m_pPool && pCharBlock < (m_pPool + (m_blockSize * m_maxObjects)));

		// Check that pBlock is aligned on a block boundary
		ASSERT_TRUE(((size_t)(pCharBlock - m_pPool) % m_blockSize) == 0);
	}
#endif

    Push(pBlock);
	m_blocksInUse--;
	m_deallocations++;
}

//------------------------------------------------------------------------------
// AccountAlloc
//------------------------------------------------------------------------------
void Allocator::AccountAlloc(bool newBlock)
{
    if (newBlock) m_blockCnt++;
    m_blocksInUse++;
    m_allocations++;
}

//------------------------------------------------------------------------------
// Push
//------------------------------------------------------------------------------
void Allocator::Push(void* pMemory)
{
    Block* pBlock = (Block*)pMemory;
    pBlock->pNext = m_pHead;
    m_pHead = pBlock;
}

//------------------------------------------------------------------------------
// Pop
//------------------------------------------------------------------------------
void* Allocator::Pop()
{
    Block* pBlock = NULL;

    if (m_pHead)
    {
        pBlock = m_pHead;
        m_pHead = m_pHead->pNext;
    }

    return (void*)pBlock;
}

} // namespace dmq





