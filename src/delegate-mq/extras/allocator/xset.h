#ifndef _XSET_H
#define _XSET_H

#include "stl_allocator.h"
#include <set>

namespace dmq {

// xset uses a fix-block memory allocator
template <typename Key, typename Compare = std::less<Key>, typename Alloc = stl_allocator<Key>>
using xset = std::set<Key, Compare, Alloc>;

template <typename Key, typename Compare = std::less<Key>, typename Alloc = stl_allocator<Key>>
using xmultiset = std::multiset<Key, Compare, Alloc>;

} // namespace dmq

#if 0  // Deprecated
template<class _Kty,
	class _Pr = std::less<_Kty>,
	class _Alloc = stl_allocator<_Kty> >
	class xset
		: public std::set<_Kty, _Pr, _Alloc>
	{
	};

/// @see xset
template<class _Kty,
	class _Pr = std::less<_Kty>,
	class _Alloc = stl_allocator<_Kty> >
	class xmultiset
		: public std::multiset<_Kty, _Pr, _Alloc>
	{
	};
#endif

#endif
