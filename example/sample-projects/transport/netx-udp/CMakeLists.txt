# transport/netx-udp — remote delegates over NetX Duo UDP on ThreadX
#
# The transport/serializer app (sender.h, receiver.h, the built-in
# serializer's data classes) on ThreadX's Linux/GNU simulation port, with
# NetXUdpTransport over NetX Duo's loopback interface. Needs ThreadX and NetX
# Duo, fetched by scripts/01_fetch_repos.py.
#
#   cmake -B build .
#   cmake --build build
#   ./build/delegate_netx_udp

cmake_minimum_required(VERSION 3.16)

if(NOT (UNIX AND NOT APPLE))
    message(FATAL_ERROR "transport/netx-udp only builds on Linux (ThreadX Linux/GNU simulation port).")
endif()

set(DELEGATE_APP "delegate_netx_udp")
project(DelegateNetXUdp VERSION 1.0 LANGUAGES CXX C ASM)

# Set build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_THREADX")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_SERIALIZE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_THREADX_UDP")
include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

# NetX Duo's own build, against the ThreadX target DelegateMQ.cmake created.
# Its FileX-based protocol servers aren't needed here. nx_user.h (this folder)
# makes NetX pass its control blocks safely on 64-bit; see that file.
set(NXD_ENABLE_FILE_SERVERS OFF CACHE BOOL "" FORCE)
set(NX_USER_FILE "${CMAKE_SOURCE_DIR}/nx_user.h")
add_subdirectory("${NETX_ROOT}" "${CMAKE_BINARY_DIR}/netxduo_build")

# The transport/serializer app, with the built-in serializer
set(APP_DIR "${CMAKE_SOURCE_DIR}/../serializer")
set(SERIALIZER_DIR "${APP_DIR}/serializer/builtin")
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/*.c*" "${CMAKE_SOURCE_DIR}/*.h"
    "${APP_DIR}/sender.h" "${APP_DIR}/receiver.h"
    "${SERIALIZER_DIR}/*.cpp" "${SERIALIZER_DIR}/*.h")
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

# This folder first: its transport.h is the one sender.h and receiver.h include
target_include_directories(${DELEGATE_APP} PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${APP_DIR}
    ${SERIALIZER_DIR}
    ${DMQ_INCLUDE_DIR}
)

# DelegateMQ's ThreadX threads run the Sender, the Receiver and their
# serializers: give them more than the 2 KB default.
target_compile_definitions(${DELEGATE_APP} PRIVATE DMQ_THREADX_STACK_BYTES=16384)

target_link_libraries(${DELEGATE_APP} PRIVATE azrtos::netxduo ${THREADX_LIBRARIES})
