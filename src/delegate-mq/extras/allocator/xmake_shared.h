#ifndef _XMAKE_SHARED_H
#define _XMAKE_SHARED_H

#include "stl_allocator.h"
#include <memory>
#include <utility>

/// @brief Create a shared_ptr with both the object and control block allocated 
///        from the fixed-block pool.
namespace dmq {
    template <typename T, typename... Args>
    inline std::shared_ptr<T> xmake_shared(Args&&... args)
    {
        return std::allocate_shared<T>(stl_allocator<T>(), std::forward<Args>(args)...);
    }
}

#endif // _XMAKE_SHARED_H
