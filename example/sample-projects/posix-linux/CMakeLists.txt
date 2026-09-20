# posix-linux — DelegateMQ feature demo on raw POSIX threads (DMQ_THREAD_POSIX)
#
# No external repo/toolchain dependency -- pthreads are part of the host libc.
# Unlike threadx-linux/freertos-linux/zephyr-linux, this needs no vendored
# kernel: it's a plain desktop CMake project like any other sample-projects/
# entry, just using DMQ_THREAD_POSIX instead of the DMQ_THREAD_STDLIB default.
#
#   cmake -B build .
#   cmake --build build
#   ./build/delegate_posix_linux

cmake_minimum_required(VERSION 3.16)

if(NOT (UNIX AND NOT APPLE))
    message(FATAL_ERROR "posix-linux only builds on Linux -- verified there. "
        "pthread_condattr_setclock(CLOCK_MONOTONIC), used by this port for "
        "wall-clock-jump-immune timed waits, is not implemented on Darwin/macOS.")
endif()

set(DELEGATE_APP "delegate_posix_linux")
project(DelegatePosixLinux VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# Set build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_POSIX")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")

find_package(Threads REQUIRED)

# Collect application source files
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/*.c*" "${CMAKE_SOURCE_DIR}/*.h")

# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

include_directories(
    .
    ${DMQ_INCLUDE_DIR}
)

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

target_link_libraries(${DELEGATE_APP} PRIVATE Threads::Threads)
