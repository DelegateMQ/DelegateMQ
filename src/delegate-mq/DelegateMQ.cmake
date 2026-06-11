# DelegateMQ cmake module 
#
# This module sets the following variables in your project:
#
#   DMQ_INCLUDE_DIR - the directory containing DelegateMQ headers.
#   DMQ_LIB_SOURCES - the core DelegateMQ delegate library files.
#   DMQ_PORT_SOURCES - the predefined supporting source code files 
#   based on the DMQ build options.
#
# Set DMQ build options:
#
#   # Optional: Set DMQ build options to override defaults.
#   set(DMQ_THREAD "DMQ_THREAD_STDLIB")
#   include("${CMAKE_SOURCE_DIR}/src/delegate-mq/DelegateMQ.cmake")
#
# Use variables to build:
#
#   # Collect DelegateMQ port/extras source files
#   list(APPEND SOURCES ${DMQ_PORT_SOURCES})
#
#   # Add include directory
#   include_directories(${DMQ_INCLUDE_DIR})

macro(check _file)
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist!")
    endif()
endmacro()

macro(set_and_check _var _file)
    set(${_var} "${_file}")
    check("${_file}")
endmacro()

set_and_check(DMQ_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set_and_check(DMQ_INCLUDE_DIR "${DMQ_ROOT_DIR}")

check("${DMQ_ROOT_DIR}/Defaults.cmake")
include ("${DMQ_ROOT_DIR}/Defaults.cmake")

check("${DMQ_ROOT_DIR}/Macros.cmake")
include ("${DMQ_ROOT_DIR}/Macros.cmake")

check("${DMQ_ROOT_DIR}/Common.cmake")
include ("${DMQ_ROOT_DIR}/Common.cmake")

check("${DMQ_ROOT_DIR}/Port.cmake")
include ("${DMQ_ROOT_DIR}/Port.cmake")

check("${DMQ_ROOT_DIR}/External.cmake")
include ("${DMQ_ROOT_DIR}/External.cmake")

# New option (Default OFF)
option(DMQ_STRICT "Enable strict DelegateMQ compiler warnings and errors" OFF)

if (DMQ_STRICT)
    message(STATUS "DelegateMQ: Strict build enabled (${DMQ_STRICT_FLAGS})")

    # Apply strict compile flags only to DelegateMQ source files
    set_source_files_properties(${DMQ_LIB_SOURCES} ${DMQ_PORT_SOURCES} PROPERTIES COMPILE_OPTIONS "${DMQ_STRICT_FLAGS}")

    # On MSVC, different warning levels cause a PCH mismatch error (C4652).
    # We disable PCH for these specific files to allow strict checking without affecting the whole target.
    if (MSVC)
        set_source_files_properties(${DMQ_LIB_SOURCES} ${DMQ_PORT_SOURCES} PROPERTIES COMPILE_FLAGS "/Y-")
    endif()
endif()

