#include "DelegateMQ.h"

#ifdef DMQ_ALLOCATOR

#include "UnitTestCommon.h"
#include "extras/allocator/Allocator.h"
#include "extras/allocator/xallocator.h"
#include "extras/allocator/stl_allocator.h"
#include "extras/allocator/xmake_shared.h"
#include "extras/allocator/xnew.h"
#include <vector>
#include <map>
#include <list>
#include <set>
#include <stdexcept>

using namespace std;
using namespace dmq;  // Allocator, AllocatorPool, stl_allocator

// A class whose constructor always throws, used to verify that xnew<T>()
// returns the pool block to the allocator instead of leaking it when T's
// constructor throws.
struct ThrowingCtor {
    ThrowingCtor() { throw std::runtime_error("ThrowingCtor"); }
};

// A class to test DECLARE_ALLOCATOR/IMPLEMENT_ALLOCATOR
class MyClass {
    DECLARE_ALLOCATOR
public:
    int x;
};

// Allocate 10 blocks of MyClass size from the heap
IMPLEMENT_ALLOCATOR(MyClass, 10, NULL)

// A class to test XALLOCATOR
class MyXClass {
    XALLOCATOR
public:
    int x;
};

void AllocatorTests()
{
    const size_t minBlockSize = sizeof(long*);

    // Test Allocator class
    {
        size_t requestSize = 32;
        Allocator allocator(requestSize, 5, NULL, "TestAllocator");
        DMQ_ASSERT_TRUE(allocator.GetBlockSize() == (requestSize < minBlockSize ? minBlockSize : requestSize));
        DMQ_ASSERT_TRUE(allocator.GetMaxBlocks() == 5);
        DMQ_ASSERT_TRUE(allocator.GetBlockCount() == 0);
        DMQ_ASSERT_TRUE(allocator.GetBlocksInUse() == 0);

        void* p1 = allocator.Allocate(requestSize);
        DMQ_ASSERT_TRUE(p1 != NULL);
        DMQ_ASSERT_TRUE(allocator.GetBlocksInUse() == 1);
        DMQ_ASSERT_TRUE(allocator.GetAllocations() == 1);
        DMQ_ASSERT_TRUE(allocator.GetBlockCount() == 1);    // pool blocks count too

        void* p2 = allocator.Allocate(requestSize);
        DMQ_ASSERT_TRUE(p2 != NULL);
        DMQ_ASSERT_TRUE(allocator.GetBlocksInUse() == 2);
        DMQ_ASSERT_TRUE(allocator.GetBlockCount() == 2);

        allocator.Deallocate(p1);
        DMQ_ASSERT_TRUE(allocator.GetBlocksInUse() == 1);
        DMQ_ASSERT_TRUE(allocator.GetDeallocations() == 1);

        allocator.Deallocate(p2);
        DMQ_ASSERT_TRUE(allocator.GetBlocksInUse() == 0);
    }

    // Test AllocatorPool
    {
        AllocatorPool<int, 5> pool;
        size_t expectedSize = sizeof(int) < minBlockSize ? minBlockSize : sizeof(int);
        DMQ_ASSERT_TRUE(pool.GetBlockSize() == expectedSize);
        
        int* p1 = (int*)pool.Allocate(sizeof(int));
        DMQ_ASSERT_TRUE(p1 != NULL);
        
        pool.Deallocate(p1);
    }

    // AllocatorPool of a type smaller than a pointer: every block, each at
    // least pointer-sized, must lie inside the pool's own memory
    {
        AllocatorPool<uint16_t, 4> pool;
        const char* begin = reinterpret_cast<const char*>(&pool);
        const char* end = begin + sizeof(pool);
        void* blocks[4];
        for (int i = 0; i < 4; i++)
        {
            blocks[i] = pool.Allocate(sizeof(uint16_t));
            DMQ_ASSERT_TRUE(blocks[i] != NULL);
            const char* p = static_cast<const char*>(blocks[i]);
            DMQ_ASSERT_TRUE(p >= begin && p + pool.GetBlockSize() <= end);
            DMQ_ASSERT_TRUE(reinterpret_cast<uintptr_t>(blocks[i]) % alignof(void*) == 0);
        }
        for (int i = 0; i < 4; i++)
            pool.Deallocate(blocks[i]);
    }

    // AllocatorPool of an odd-sized type: blocks are rounded up so each
    // block's free-list pointer is aligned
    {
        struct Odd { char c[13]; };
        AllocatorPool<Odd, 3> pool;
        DMQ_ASSERT_TRUE(pool.GetBlockSize() % alignof(void*) == 0);
        void* blocks[3];
        for (int i = 0; i < 3; i++)
        {
            blocks[i] = pool.Allocate(sizeof(Odd));
            DMQ_ASSERT_TRUE(blocks[i] != NULL);
            DMQ_ASSERT_TRUE(reinterpret_cast<uintptr_t>(blocks[i]) % alignof(void*) == 0);
        }
        for (int i = 0; i < 3; i++)
            pool.Deallocate(blocks[i]);
    }

    // Test DECLARE_ALLOCATOR/IMPLEMENT_ALLOCATOR macros
    {
        MyClass* p1 = new MyClass();
        DMQ_ASSERT_TRUE(p1 != NULL);
        delete p1;
    }

    // Test xallocator (C API)
    {
        void* p1 = xmalloc(10);
        DMQ_ASSERT_TRUE(p1 != NULL);
        
        // p1 should handle up to 10 bytes (or block size power of 2)
        // xrealloc to smaller or same size
        void* p2 = xrealloc(p1, 5);
        DMQ_ASSERT_TRUE(p2 != NULL);
        
        // xrealloc to larger size
        void* p3 = xrealloc(p2, 100);
        DMQ_ASSERT_TRUE(p3 != NULL);

        // xrealloc(NULL, size) should be like xmalloc
        void* p4 = xrealloc(NULL, 50);
        DMQ_ASSERT_TRUE(p4 != NULL);

        // xrealloc(p, 0) should be like xfree
        void* p5 = xrealloc(p4, 0);
        DMQ_ASSERT_TRUE(p5 == NULL);
        
        xfree(p3);
    }

    // xalloc_get_pool_stats(): a block in use shows in its block size's entry,
    // peak never below in use, entries smallest first
    {
        void* p = xmalloc(100);
        DMQ_ASSERT_TRUE(p != NULL);
        const uint32_t blockSize = static_cast<uint32_t>(xallocator_get_allocator(100)->GetBlockSize());

        XallocPoolStats stats[32];
        const size_t n = xalloc_get_pool_stats(stats, 32);
        DMQ_ASSERT_TRUE(n > 0);
        bool found = false;
        for (size_t i = 0; i < n; i++) {
            DMQ_ASSERT_TRUE(stats[i].peakBlocks >= stats[i].blocksInUse);
            if (i > 0)
                DMQ_ASSERT_TRUE(stats[i].blockSize > stats[i - 1].blockSize);
#if defined(DMQ_XALLOCATOR_STATIC_POOLS)
            DMQ_ASSERT_TRUE(stats[i].capacity > 0 && stats[i].peakBlocks <= stats[i].capacity);
#else
            DMQ_ASSERT_TRUE(stats[i].capacity == 0);   // heap-backed: no fixed limit
#endif
            if (stats[i].blockSize == blockSize) {
                found = true;
                DMQ_ASSERT_TRUE(stats[i].blocksInUse >= 1);
            }
        }
        DMQ_ASSERT_TRUE(found);
        DMQ_ASSERT_TRUE(xalloc_get_pool_stats(stats, 0) == 0);
        DMQ_ASSERT_TRUE(xalloc_get_pool_stats(NULL, 32) == 0);
        xfree(p);
    }

    // Test stl_allocator with list
    {
        std::list<int, stl_allocator<int>> l;
        l.push_back(1);
        l.push_back(2);
        l.push_back(3);
        DMQ_ASSERT_TRUE(l.size() == 3);
        l.pop_front();
        DMQ_ASSERT_TRUE(l.size() == 2);
    }

    // Test stl_allocator with set
    {
        std::set<int, std::less<int>, stl_allocator<int>> s;
        s.insert(10);
        s.insert(20);
        DMQ_ASSERT_TRUE(s.size() == 2);
    }

    // Test XALLOCATOR macro
    {
        MyXClass* p1 = new MyXClass();
        DMQ_ASSERT_TRUE(p1 != NULL);
        delete p1;
    }

    // Test stl_allocator with vector
    {
        std::vector<int, stl_allocator<int>> v;
        v.push_back(1);
        v.push_back(2);
        DMQ_ASSERT_TRUE(v.size() == 2);
        v.clear();
    }

    // Test stl_allocator with map
    {
        std::map<int, int, std::less<int>, stl_allocator<std::pair<const int, int>>> m;
        m[1] = 10;
        m[2] = 20;
        DMQ_ASSERT_TRUE(m.size() == 2);
    }

    // Test xmake_shared
    {
        auto sp = dmq::xmake_shared<int>(123);
        DMQ_ASSERT_TRUE(sp != NULL);
        DMQ_ASSERT_TRUE(*sp == 123);
    }

    // Test xnew<T>() returns the pool block instead of leaking it when T's
    // constructor throws (regression test for the xnew.h leak-on-throw fix).
    // xnew<T>() only guards against a throwing constructor when exceptions
    // are available and DMQ_ASSERTS is off (see xnew.h); under DMQ_ASSERTS
    // the library assumes constructors never throw, so this check does not
    // apply to that build configuration.
#if defined(__cpp_exceptions) && !defined(DMQ_ASSERTS)
    {
        Allocator* allocator = xallocator_get_allocator(sizeof(ThrowingCtor));
        uint32_t before = allocator->GetBlocksInUse();

        bool caught = false;
        try {
            dmq::xnew<ThrowingCtor>();
        }
        catch (const std::runtime_error&) {
            caught = true;
        }
        DMQ_ASSERT_TRUE(caught);
        DMQ_ASSERT_TRUE(allocator->GetBlocksInUse() == before);
    }
#endif

    // Deallocate(NULL) is ignored: a delete-expression on a null pointer may
    // call DECLARE_ALLOCATOR's operator delete(NULL), which used to Push(NULL)
    // and write through it
    {
        Allocator allocator(32, 2);
        allocator.Deallocate(NULL);
        DMQ_ASSERT_TRUE(allocator.GetBlocksInUse() == 0);
        DMQ_ASSERT_TRUE(allocator.GetDeallocations() == 0);
        MyClass::operator delete(nullptr);

        void* p = allocator.Allocate(32);
        DMQ_ASSERT_TRUE(p != NULL);
        allocator.Deallocate(p);
    }

    // A size too large to round up to a power of two returns NULL. It used to
    // wrap nexthigher() to 0 and return a tiny block the caller would overrun.
    {
        DMQ_ASSERT_TRUE(xallocator_get_allocator(static_cast<size_t>(-1)) == NULL);
        DMQ_ASSERT_TRUE(xallocator_get_allocator(static_cast<size_t>(-1) / 2) == NULL);
        DMQ_ASSERT_TRUE(xmalloc(static_cast<size_t>(-1)) == NULL);
        DMQ_ASSERT_TRUE(xmalloc(static_cast<size_t>(-1) / 2) == NULL);
    }

#ifdef DMQ_ALLOCATOR_SAFEGUARDS
    // White-box Safeguard Tests — constants come from xallocator.h (single source of truth)
    {
        size_t size = 16;
        void* p = xmalloc(size);
        DMQ_ASSERT_TRUE(p != NULL);

        // Check magic and canary in header
        // Header layout (safeguards ON, 64-bit): [allocator*:8][magic:4][canary:4] = 16 bytes
        uint32_t* pMagic = (uint32_t*)((char*)p - 8);
        uint32_t* pCanaryFront = (uint32_t*)((char*)p - 4);
        DMQ_ASSERT_TRUE(*pMagic == XALLOC_MAGIC);
        DMQ_ASSERT_TRUE(*pCanaryFront == XALLOC_CANARY);

        // Check canary in footer
        // nexthigher(16 + XALLOC_BLOCK_HEADER_SIZE + XALLOC_BLOCK_FOOTER_SIZE) = nexthigher(36) = 64
        // userSize = 64 - 16 - 4 = 44 bytes
        Allocator* allocator = xallocator_get_allocator(size);
        size_t userSize = allocator->GetBlockSize() - XALLOC_BLOCK_HEADER_SIZE - XALLOC_BLOCK_FOOTER_SIZE;
        uint32_t* pCanaryBack = (uint32_t*)((char*)p + userSize);
        DMQ_ASSERT_TRUE(*pCanaryBack == XALLOC_CANARY);

        xfree(p);
    }
#endif

#ifdef DMQ_ALLOCATOR_SAFEGUARDS
    // Manual Safeguard Tests — each block intentionally triggers FaultHandler.
    {
        void* p = xmalloc(16);
        Allocator* allocator = xallocator_get_allocator(16);
        size_t userSize = allocator->GetBlockSize() - XALLOC_BLOCK_HEADER_SIZE - XALLOC_BLOCK_FOOTER_SIZE;
        ((char*)p)[userSize] = 0; // Buffer overrun — corrupts footer canary
        xfree(p);                 // Should trigger FaultHandler
    }
    {
        void* p = xmalloc(16);
        xfree(p);
        xfree(p);           // Double free — Should trigger FaultHandler
    }
#endif
}

#endif
