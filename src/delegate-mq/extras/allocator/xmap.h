#ifndef _XMAP_H
#define _XMAP_H

#include "stl_allocator.h"
#include <map>

namespace dmq {

// xmap uses a fix-block memory allocator
template <typename Key, typename Value, typename Alloc = stl_allocator<std::pair<const Key, Value>>>
using xmap = std::map<Key, Value, std::less<Key>, Alloc>;

template <typename Key, typename Value, typename Alloc = stl_allocator<std::pair<const Key, Value>>>
using xmultimap = std::multimap<Key, Value, std::less<Key>, Alloc>;

} // namespace dmq

#if 0  // Deprecated
template<class _Kty,
	class _Ty,
	class _Pr = std::less<_Kty>,
	class _Alloc = stl_allocator<std::pair<const _Kty, _Ty> > >
	class xmap
		: public std::map<_Kty, _Ty, _Pr, _Alloc>
	{
	};

template<class _Kty,
	class _Ty,
	class _Pr = std::less<_Kty>,
	class _Alloc = stl_allocator<std::pair<const _Kty, _Ty> > >
	class xmultimap
		: public std::multimap<_Kty, _Ty, _Pr, _Alloc>
	{
	};
#endif

#endif

