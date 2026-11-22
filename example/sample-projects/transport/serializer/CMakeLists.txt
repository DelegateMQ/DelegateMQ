# Remote delegates over a choice of transport and serializer. One source set;
# pick the combination at configure time, one build directory per combination:
#
#   cmake -B build-udp-builtin     -DSAMPLE_TRANSPORT=udp    -DSAMPLE_SERIALIZER=builtin .
#   cmake -B build-zeromq-bitsery  -DSAMPLE_TRANSPORT=zeromq -DSAMPLE_SERIALIZER=bitsery .
#
# SAMPLE_TRANSPORT:  udp, tcp (Linux or Win32 sockets, picked by the host),
#                    pipe (Windows named pipe), uds (Linux Unix domain
#                    socket), lwip and lwip-netconn (UDP over the lwIP
#                    stack's socket or netconn API, Linux), zeromq, nng
# SAMPLE_SERIALIZER: builtin (DelegateMQ's own serialize class, no
#                    dependencies), bitsery, cereal, msgpack, rapidjson
#
# The transport-specific code is in transport/<name>/transport.h, the data
# classes for each serializer in serializer/<name>/. See README.md.
cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_app")
project(Delegate VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

set(SAMPLE_TRANSPORT "udp" CACHE STRING "udp, tcp, pipe, uds, lwip, lwip-netconn, zeromq or nng")
set(SAMPLE_SERIALIZER "builtin" CACHE STRING "builtin, bitsery, cereal, msgpack or rapidjson")
set_property(CACHE SAMPLE_TRANSPORT PROPERTY STRINGS udp tcp pipe uds lwip lwip-netconn zeromq nng)
set_property(CACHE SAMPLE_SERIALIZER PROPERTY STRINGS builtin bitsery cereal msgpack rapidjson)

if (MSVC)
    add_compile_options("/Zc:__cplusplus")
endif()

# Transport -> DelegateMQ's DMQ_TRANSPORT option
if (SAMPLE_TRANSPORT STREQUAL "udp")
    if (WIN32)
        set(DMQ_TRANSPORT "DMQ_TRANSPORT_WIN32_UDP")
    else()
        set(DMQ_TRANSPORT "DMQ_TRANSPORT_LINUX_UDP")
    endif()
elseif (SAMPLE_TRANSPORT STREQUAL "tcp")
    if (WIN32)
        set(DMQ_TRANSPORT "DMQ_TRANSPORT_WIN32_TCP")
    else()
        set(DMQ_TRANSPORT "DMQ_TRANSPORT_LINUX_TCP")
    endif()
elseif (SAMPLE_TRANSPORT STREQUAL "pipe")
    if (NOT WIN32)
        message(FATAL_ERROR "SAMPLE_TRANSPORT=pipe is a Windows named pipe; Windows only")
    endif()
    set(DMQ_TRANSPORT "DMQ_TRANSPORT_WIN32_PIPE")
elseif (SAMPLE_TRANSPORT STREQUAL "uds")
    if (NOT (UNIX AND NOT APPLE))
        message(FATAL_ERROR "SAMPLE_TRANSPORT=uds is a Linux Unix domain socket; Linux only")
    endif()
    set(DMQ_TRANSPORT "DMQ_TRANSPORT_LINUX_UDS")
elseif (SAMPLE_TRANSPORT STREQUAL "lwip" OR SAMPLE_TRANSPORT STREQUAL "lwip-netconn")
    if (NOT (UNIX AND NOT APPLE))
        message(FATAL_ERROR "SAMPLE_TRANSPORT=${SAMPLE_TRANSPORT} runs lwIP's Unix port; Linux only")
    endif()
    if (SAMPLE_TRANSPORT STREQUAL "lwip")
        set(DMQ_TRANSPORT "DMQ_TRANSPORT_ARM_LWIP_UDP")
    else()
        set(DMQ_TRANSPORT "DMQ_TRANSPORT_ARM_LWIP_NETCONN_UDP")
    endif()
elseif (SAMPLE_TRANSPORT STREQUAL "zeromq")
    set(DMQ_TRANSPORT "DMQ_TRANSPORT_ZEROMQ")
elseif (SAMPLE_TRANSPORT STREQUAL "nng")
    set(DMQ_TRANSPORT "DMQ_TRANSPORT_NNG")
else()
    message(FATAL_ERROR "Unknown SAMPLE_TRANSPORT '${SAMPLE_TRANSPORT}' (udp, tcp, pipe, uds, lwip, lwip-netconn, zeromq, nng)")
endif()

# Serializer -> DelegateMQ's DMQ_SERIALIZE option
if (SAMPLE_SERIALIZER STREQUAL "builtin")
    set(DMQ_SERIALIZE "DMQ_SERIALIZE_SERIALIZE")
elseif (SAMPLE_SERIALIZER STREQUAL "bitsery")
    set(DMQ_SERIALIZE "DMQ_SERIALIZE_BITSERY")
elseif (SAMPLE_SERIALIZER STREQUAL "cereal")
    set(DMQ_SERIALIZE "DMQ_SERIALIZE_CEREAL")
elseif (SAMPLE_SERIALIZER STREQUAL "msgpack")
    set(DMQ_SERIALIZE "DMQ_SERIALIZE_MSGPACK")
elseif (SAMPLE_SERIALIZER STREQUAL "rapidjson")
    set(DMQ_SERIALIZE "DMQ_SERIALIZE_RAPIDJSON")
else()
    message(FATAL_ERROR "Unknown SAMPLE_SERIALIZER '${SAMPLE_SERIALIZER}' (builtin, bitsery, cereal, msgpack, rapidjson)")
endif()

message(STATUS "transport/serializer: ${SAMPLE_TRANSPORT} transport, ${SAMPLE_SERIALIZER} serializer")

# Set build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_STDLIB")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

set(TRANSPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/transport/${SAMPLE_TRANSPORT}")
set(SERIALIZER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/serializer/${SAMPLE_SERIALIZER}")

# The app, plus this combination's transport and serializer files
file(GLOB SOURCES "*.cpp" "*.h" "${TRANSPORT_DIR}/*.h" "${SERIALIZER_DIR}/*.cpp" "${SERIALIZER_DIR}/*.h")
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

# transport.h and data.h are found through these
target_include_directories(${DELEGATE_APP} PRIVATE
    ${TRANSPORT_DIR}
    ${SERIALIZER_DIR}
    ${DMQ_INCLUDE_DIR}
)

# Third-party serializer headers
if (SAMPLE_SERIALIZER STREQUAL "bitsery")
    target_include_directories(${DELEGATE_APP} PRIVATE ${BITSERY_INCLUDE_DIR})
elseif (SAMPLE_SERIALIZER STREQUAL "cereal")
    target_include_directories(${DELEGATE_APP} PRIVATE ${CEREAL_INCLUDE_DIR})
elseif (SAMPLE_SERIALIZER STREQUAL "msgpack")
    target_include_directories(${DELEGATE_APP} PRIVATE ${MSGPACK_INCLUDE_DIR})
elseif (SAMPLE_SERIALIZER STREQUAL "rapidjson")
    target_include_directories(${DELEGATE_APP} PRIVATE ${RAPIDJSON_INCLUDE_DIR})
endif()

# Third-party transport libraries
if (SAMPLE_TRANSPORT STREQUAL "zeromq")
    target_include_directories(${DELEGATE_APP} PRIVATE ${ZeroMQ_INCLUDE_DIR})
    target_link_libraries(${DELEGATE_APP} PRIVATE libzmq)
elseif (SAMPLE_TRANSPORT STREQUAL "nng")
    find_library(NNG_LIB nng HINTS ${NNG_LIBRARY_DIR})
    if (NOT NNG_LIB)
        message(FATAL_ERROR "Could not find the nng library in ${NNG_LIBRARY_DIR}")
    endif()
    target_include_directories(${DELEGATE_APP} PRIVATE ${NNG_INCLUDE_DIR})
    target_compile_definitions(${DELEGATE_APP} PRIVATE NNG_STATIC_LIB)
    target_link_libraries(${DELEGATE_APP} PRIVATE ${NNG_LIB})
    if (WIN32)
        target_link_libraries(${DELEGATE_APP} PRIVATE ws2_32)
    endif()
elseif (SAMPLE_TRANSPORT MATCHES "^lwip")
    # lwIP itself (LWIP_SOURCES, from DelegateMQ's External.cmake) plus its
    # Unix port's sys_arch.c (threads, semaphores and mailboxes on pthreads),
    # as a C library with the lwipopts.h and lwip_init.h in transport/lwip.
    enable_language(C)
    set(LWIP_SAMPLE_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/transport/lwip"
        ${LWIP_INCLUDE_DIRS}
        "${LWIP_ROOT}/contrib/ports/unix/posixlib/include")
    add_library(lwip_host STATIC ${LWIP_SOURCES} "${LWIP_ROOT}/contrib/ports/unix/port/sys_arch.c")
    target_include_directories(lwip_host PUBLIC ${LWIP_SAMPLE_INCLUDE_DIRS})
    find_package(Threads REQUIRED)
    target_link_libraries(lwip_host PUBLIC Threads::Threads)
    target_link_libraries(${DELEGATE_APP} PRIVATE lwip_host)
endif()
