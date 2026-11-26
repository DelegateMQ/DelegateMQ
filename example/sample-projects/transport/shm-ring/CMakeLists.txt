# Remote delegates between two processes over shared memory: Win32ShmTransport
# (file mapping + named events) on Windows, LinuxShmTransport (shm_open + named
# semaphores) on Linux. See README.md.
cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_app")
project(Delegate VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

if (NOT WIN32 AND NOT (UNIX AND NOT APPLE))
    message(FATAL_ERROR "transport/shm-ring: Windows or Linux only")
endif()

# Build options. The shared-memory transport sits beside any DMQ_TRANSPORT
# choice; none is needed here. raw::Serializer is always available.
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_STDLIB")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

file(GLOB SOURCES "*.cpp" "*.h")
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

include_directories(${DMQ_INCLUDE_DIR})

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

if (UNIX)
    # shm_open/sem_open: in libc since glibc 2.34, librt before
    find_library(RT_LIB rt)
    if (RT_LIB)
        target_link_libraries(${DELEGATE_APP} PRIVATE ${RT_LIB})
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(${DELEGATE_APP} PRIVATE Threads::Threads)
endif()
