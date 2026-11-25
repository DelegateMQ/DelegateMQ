# databus/local: single-process DataBus sample (no transport or serializer
# between the components; the optional SpyBridge sends a copy to dmq-spy).
#
#   cmake -B build .
#   cmake --build build --config Release
#   ./build/delegate_databus_local                 Linux (single-config: Makefiles, Ninja)
#   build\Release\delegate_databus_local.exe       Windows (Visual Studio)

cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_databus_local")
project(DelegateDataBusLocal VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DelegateMQ build options: threading + DataBus, no transport or serializer.
# Pass -DDMQ_DATABUS_TOOLS=OFF to build without SpyBridge / dmq-spy support.
set(DMQ_UTIL "ON")
set(DMQ_DATABUS "ON")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/*.cpp" "${CMAKE_SOURCE_DIR}/*.h")

if (DMQ_DATABUS_TOOLS)
    add_compile_definitions(DMQ_DATABUS_TOOLS)
    set(SPY_BRIDGE_DIR "${CMAKE_SOURCE_DIR}/../../../../tools")
    set(SPY_BRIDGE_SOURCES
        "${SPY_BRIDGE_DIR}/bridge/spy/SpyBridge.cpp"
        "${SPY_BRIDGE_DIR}/bridge/spy/SpyBridge.h"
    )
    list(APPEND SOURCES ${SPY_BRIDGE_SOURCES})
endif()

list(APPEND SOURCES ${DMQ_PORT_SOURCES})

source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

target_include_directories(${DELEGATE_APP} PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${DMQ_INCLUDE_DIR}"
)

if (DMQ_DATABUS_TOOLS)
    target_include_directories(${DELEGATE_APP} PRIVATE
        "${SPY_BRIDGE_DIR}/net"
        "${SPY_BRIDGE_DIR}/bridge/spy"
    )
endif()

find_package(Threads REQUIRED)
if (WIN32 AND DMQ_DATABUS_TOOLS)
    target_link_libraries(${DELEGATE_APP} PRIVATE ws2_32 Threads::Threads)
else()
    target_link_libraries(${DELEGATE_APP} PRIVATE Threads::Threads)
endif()

if (MSVC)
    target_compile_options(${DELEGATE_APP} PRIVATE /utf-8)
endif()
