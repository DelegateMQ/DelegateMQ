#ifndef _XSTRING_H
#define _XSTRING_H

#include "stl_allocator.h"
#include <string>

// std::string::traits_type, not std::char_traits<char>: GCC with libc++ 17+
// warns on naming std::char_traits<char> (its primary template is deprecated).
namespace dmq {
    typedef std::basic_string<char, std::string::traits_type, stl_allocator<char> > xstring;
}
// xwstring is unused by DelegateMQ. Uncomment if wide string support is needed by the application.
// typedef std::basic_string<wchar_t, std::wstring::traits_type, stl_allocator<wchar_t> > xwstring;

#endif 

