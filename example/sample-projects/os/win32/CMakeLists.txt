# os/win32 — DelegateMQ feature demo on native Win32 threads (DMQ_THREAD_WIN32)
#
# No external dependency: the Win32 API is part of Windows. A plain desktop
# CMake project, like os/posix, using DMQ_THREAD_WIN32 instead of the
# DMQ_THREAD_STDLIB default.
#
#   cmake -B build .
#   cmake --build build --config Release
#   build\Release\delegate_win32.exe

cmake_minimum_required(VERSION 3.16)

if(NOT WIN32)
    message(FATAL_ERROR "os/win32 only builds on Windows (DMQ_THREAD_WIN32 uses the Win32 API).")
endif()

set(DELEGATE_APP "delegate_win32")
project(DelegateWin32 VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# Set build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_WIN32")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

# Collect application source files
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/*.c*" "${CMAKE_SOURCE_DIR}/*.h")

# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# The portable tests every OS port sample runs (test/portable)
include("${CMAKE_SOURCE_DIR}/../../../../test/portable/PortableTests.cmake")
list(APPEND SOURCES ${PORTABLE_TESTS_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

include_directories(
    .
    ${DMQ_INCLUDE_DIR}
    ${PORTABLE_TESTS_DIR}
)

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})
