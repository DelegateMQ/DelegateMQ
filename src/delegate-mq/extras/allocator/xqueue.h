#ifndef _XQUEUE_H
#define _XQUEUE_H

#include "stl_allocator.h"
#include <queue>
#include <list>

namespace dmq {

// xqueue uses a fix-block memory allocator
template <typename T, typename Alloc = stl_allocator<T>>
using xqueue = std::queue<T, std::deque<T, Alloc>>;

} // namespace dmq

#if 0  // Deprecated
template<class _Tp,
	class _Sequence = std::list<_Tp, stl_allocator<_Tp> > >
	class xqueue
		: public std::queue<_Tp, _Sequence>
	{
	};
#endif

#endif

