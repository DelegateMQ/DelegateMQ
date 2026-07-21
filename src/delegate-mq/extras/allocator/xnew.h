#ifndef _XNEW_H
#define _XNEW_H

// @see https://github.com/DelegateMQ/DelegateMQ
// David Lafreniere, 2026.

/// @file xnew.h
/// @brief Fixed-block pool equivalents of operator new / delete.
///
/// @details
/// xnew<T>(args...) allocates from the xallocator fixed-block pool and
/// constructs T in-place via placement new. xdelete<T>(p) calls the
/// destructor then returns the memory to the pool via xfree().
///
/// These are the pool-aware counterparts of new(std::nothrow) / delete
/// for types that do not carry the XALLOCATOR macro (e.g. user argument
/// types marshalled through make_tuple_heap).

#include "xallocator.h"

namespace dmq {
    /// @brief Allocate and construct T in the fixed-block pool.
    /// @return Pointer to the constructed object, or nullptr if xmalloc fails.
    template<typename T, typename... Args>
    inline T* xnew(Args&&... args)
    {
        void* mem = xmalloc(sizeof(T));
        if (!mem) return nullptr;

#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
        // Exceptions unavailable/disabled: T's constructor is assumed not to throw.
        return ::new(mem) T(std::forward<Args>(args)...);
#else
        try {
            return ::new(mem) T(std::forward<Args>(args)...);
        }
        catch (...) {
            // Return the block to the pool before propagating -- otherwise a
            // throwing T constructor permanently leaks a fixed-block-pool slot.
            xfree(mem);
            throw;
        }
#endif
    }

    /// @brief Destroy T and return its memory to the fixed-block pool.
    template<typename T>
    inline void xdelete(T* p)
    {
        if (p)
        {
            p->~T();
            xfree(const_cast<void*>(static_cast<const void*>(p)));
        }
    }
}

#endif // _XNEW_H
