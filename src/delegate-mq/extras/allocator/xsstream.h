#ifndef _XSSTREAM_H
#define _XSSTREAM_H

#include "stl_allocator.h"
#include <sstream>

// std::string::traits_type, not std::char_traits<char>: GCC with libc++ 17+
// warns on naming std::char_traits<char> (its primary template is deprecated).
namespace dmq {
    typedef std::basic_stringstream<char, std::string::traits_type, stl_allocator<char> > xstringstream;
    typedef std::basic_ostringstream<char, std::string::traits_type, stl_allocator<char> > xostringstream;
}
// xwstringstream/xwostringstream are unused by DelegateMQ. Uncomment if wide string support is needed by the application.
// typedef std::basic_stringstream<wchar_t, std::wstring::traits_type, stl_allocator<wchar_t> > xwstringstream;
// typedef std::basic_ostringstream<wchar_t, std::wstring::traits_type, stl_allocator<wchar_t> > xwostringstream;


#endif

