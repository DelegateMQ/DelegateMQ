cmake_minimum_required(VERSION 3.16)

# On Windows with Clang (GNU front-end), CMake requires rc.exe (Windows Resource
# Compiler) which lives in the Windows SDK — not on PATH by default outside a VS
# Developer Prompt.  Locate it automatically so a plain command prompt works.
# Must be done before project() which triggers compiler/RC detection.
if(WIN32 AND NOT CMAKE_RC_COMPILER)
    file(GLOB _rc_hints "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
    list(SORT _rc_hints ORDER DESCENDING)   # pick the newest SDK first
    find_program(CMAKE_RC_COMPILER NAMES rc HINTS ${_rc_hints})
endif()

set(DELEGATE_APP "delegate_app")
project(Delegate VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# DelegateMQ options — stdlib threading, no serialization, no transport
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_STDLIB")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")

include("${CMAKE_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")

# Collect all .cpp and .h source files in this directory
file(GLOB SOURCES "*.cpp" "*.h")

# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

# Add subdirectories to include path
include_directories(${DMQ_INCLUDE_DIR})

# Add an executable target
add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

target_compile_features(${DELEGATE_APP} PRIVATE cxx_std_17)
