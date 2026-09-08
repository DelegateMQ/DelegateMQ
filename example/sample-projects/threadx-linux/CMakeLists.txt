# threadx-linux — DelegateMQ feature demo on ThreadX (Linux/GNU simulation port)
#
# Requires the ThreadX source tree fetched by 01_fetch_repos.py at
# ../../../../threadx (i.e. sibling to the DelegateMQ workspace root).
#
# Linux/GCC only: the ThreadX Linux port implements scheduling on POSIX
# threads/signals and requires an assembler; there is no MSVC build.
#
#   mkdir build && cd build
#   cmake ..
#   cmake --build .

cmake_minimum_required(VERSION 3.16)

if(NOT (UNIX AND NOT APPLE))
    message(FATAL_ERROR "threadx-linux only builds on Linux (ThreadX Linux/GNU simulation port).")
endif()

set(DELEGATE_APP "delegate_threadx_linux")
project(DelegateThreadXLinux VERSION 1.0 LANGUAGES CXX C)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# Set build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_THREADX")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")

# Collect application source files
file(GLOB SOURCES "*.c*" "*.h")

# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

include_directories(
    ${DMQ_INCLUDE_DIR}
)

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

target_link_libraries(${DELEGATE_APP} PRIVATE ${THREADX_LIBRARIES})
