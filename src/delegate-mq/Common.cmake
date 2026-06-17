# Set C++ standard 
# C++17 minimum required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Global silence for MSVC deprecation warnings (Standard for latest compilers)
if (MSVC)
    add_compile_options("/utf-8")
    add_compile_options("/MP")
    set(DMQ_STRICT_FLAGS "/W4" "/WX" "/analyze" "/permissive-" "/Zc:__cplusplus" "/Zc:inline")
    add_compile_definitions(_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
    add_compile_definitions(_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS)

    # Fix for spdlog bundled fmt in C++20 mode
    add_compile_definitions(FMT_USE_ITERATOR_TRAITS=1)
    add_compile_definitions(FMT_MSVC_CRT_ITERATORS=0)
    add_compile_definitions(_SECURE_SCL=0)
else()
    # Add -rdynamic to ensure function names are available in stack traces on Linux
    set(DMQ_STRICT_FLAGS "-Wall" "-Wextra" "-Werror" "-Wpedantic" "-Wconversion" "-Wshadow" "-rdynamic")
endif()



