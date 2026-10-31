# C++17 minimum. A newer standard the including project already chose (e.g.
# set(CMAKE_CXX_STANDARD 20) before including DelegateMQ.cmake) is kept; an
# older one is raised to 17, which DelegateMQ needs. 98 is listed explicitly
# because it compares as greater than 17.
if(NOT DEFINED CMAKE_CXX_STANDARD OR CMAKE_CXX_STANDARD MATCHES "^(98|11|14)$")
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED True)
endif()

# Global silence for MSVC deprecation warnings (Standard for latest compilers)
if (MSVC)
    # /utf-8 travels with the dmq_add_library() target too (DelegateMQ headers
    # are UTF-8); /MP is a build-speed choice left to the including project.
    list(APPEND DMQ_COMPILE_OPTIONS "/utf-8")
    if(NOT DMQ_NO_GLOBAL_DEFINITIONS)
        add_compile_options("/utf-8")
        add_compile_options("/MP")
    endif()
    set(DMQ_STRICT_FLAGS "/W4" "/WX" "/analyze" "/analyze:external-" "/permissive-" "/Zc:__cplusplus" "/Zc:inline")
    dmq_compile_definitions(_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
    dmq_compile_definitions(_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS)

    # Fix for spdlog bundled fmt in C++20 mode. Kept directory-wide only: these
    # change iterator checking for everything they reach, so the
    # dmq_add_library() target carries them only when DMQ_LOG pulls in spdlog.
    if(NOT DMQ_NO_GLOBAL_DEFINITIONS)
        add_compile_definitions(FMT_USE_ITERATOR_TRAITS=1)
        add_compile_definitions(FMT_MSVC_CRT_ITERATORS=0)
        add_compile_definitions(_SECURE_SCL=0)
    endif()
    if(DMQ_LOG STREQUAL "ON")
        list(APPEND DMQ_COMPILE_DEFINITIONS FMT_USE_ITERATOR_TRAITS=1 FMT_MSVC_CRT_ITERATORS=0 _SECURE_SCL=0)
    endif()
else()
    # Add -rdynamic to ensure function names are available in stack traces on Linux
    set(DMQ_STRICT_FLAGS "-Wall" "-Wextra" "-Werror" "-Wpedantic" "-Wconversion" "-Wshadow" "-rdynamic")
endif()



