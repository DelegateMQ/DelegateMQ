#ifndef _STL_ALLOCATOR_H
#define _STL_ALLOCATOR_H

// @see https://github.com/endurodave/stl_allocator
// David Lafreniere

#include "xallocator.h"
#include "delegate/DelegateOpt.h"
#include <memory>    // For std::allocator and std::allocator_traits
#include <utility>   // For std::forward

namespace dmq {

// Forward declaration for stl_allocator<void>
template <typename T>
class stl_allocator;

// Specialization for `void`, but we no longer need to define `pointer` and `const_pointer`
template <>
class stl_allocator<void> 
{
public:
	typedef void value_type;

	template <class U>
	struct rebind { typedef stl_allocator<U> other; };
};

// Define the custom stl_allocator inheriting from std::allocator
template <typename T>
class stl_allocator : public std::allocator<T> 
{
public:
	typedef size_t size_type;
	typedef ptrdiff_t difference_type;
	typedef T* pointer;
	typedef const T* const_pointer;
	typedef T& reference;
	typedef const T& const_reference;
	typedef T value_type;

	// Default constructor
	stl_allocator() {}

	// Copy constructor
	template <class U>
	stl_allocator(const stl_allocator<U>&) {}

	// Rebind struct
	template <class U>
	struct rebind { typedef stl_allocator<U> other; };

	// Override allocate method to use custom allocation function
	pointer allocate(size_type n, typename std::allocator_traits<stl_allocator<void>>::const_pointer /*hint*/ = 0) 
	{
		void* p = xmalloc(n * sizeof(T));
		if (!p) BAD_ALLOC();
		return static_cast<pointer>(p);
	}

	// Override deallocate method to use custom deallocation function
	void deallocate(pointer p, size_type /*n*/) 
	{
		xfree(p);
	}

	// You can inherit other methods like construct and destroy from std::allocator
};

// Comparison operators for compatibility
template <typename T, typename U>
inline bool operator==(const stl_allocator<T>&, const stl_allocator<U>&) { return true; }

template <typename T, typename U>
inline bool operator!=(const stl_allocator<T>&, const stl_allocator<U>&) { return false; }

} // namespace dmq

#endif 
