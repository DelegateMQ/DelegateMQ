# example/sensor-node: the desktop build (Windows, Linux) of the sensor node and
# its PC client. The node's ESP32-S3 build is in node/platform/esp32s3.
#
#   cmake -B build .
#   cmake --build build --config Release
#   build/Release/sensor_node 0          (one terminal; Windows: build\Release\...)
#   build/Release/sensor_pc 0            (another)

cmake_minimum_required(VERSION 3.16)
project(SensorNode VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sensor_node)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The node's thread port: NONE (the default) builds the desktop node and the PC
# client with stdlib threads. THREADX builds only the node, on ThreadX's Linux
# port (a Linux process running the real ThreadX kernel), still with host UDP
# sockets: run it with a default build's sensor_pc.
#
#   cmake -B build-threadx -DSENSOR_NODE_RTOS=THREADX .     (Linux)
set(SENSOR_NODE_RTOS "NONE" CACHE STRING "The node's thread port: NONE (desktop threads) or THREADX")
set_property(CACHE SENSOR_NODE_RTOS PROPERTY STRINGS NONE THREADX)
if(SENSOR_NODE_RTOS STREQUAL "THREADX")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "SENSOR_NODE_RTOS=THREADX uses ThreadX's Linux port: Linux only")
    endif()
    set(DMQ_THREAD "DMQ_THREAD_THREADX")
    set(DMQ_TRANSPORT "DMQ_TRANSPORT_LINUX_UDP")
endif()

# DelegateMQ: the built-in serializer, UDP and the DataBus, on the thread port
# above. One library, shared by the programs.
set(DMQ_DATABUS "ON")
set(DMQ_NO_GLOBAL_DEFINITIONS ON)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../src/delegate-mq/DelegateMQ.cmake")
dmq_add_library(sensor_dmq)

find_package(Threads REQUIRED)
target_link_libraries(sensor_dmq PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(sensor_dmq PUBLIC ws2_32)
endif()

set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")
file(GLOB COMMON_HEADERS "${COMMON_DIR}/*.h")

# The node: the portable app (node/app) on the desktop platform, or on ThreadX
# (node/platform/threadx, with the desktop stand-ins for the hardware)
file(GLOB NODE_APP_SOURCES "node/app/*.cpp" "node/app/*.h")
file(GLOB NODE_DESKTOP_SOURCES "node/platform/desktop/*.cpp" "node/platform/desktop/*.h")
if(SENSOR_NODE_RTOS STREQUAL "THREADX")
    file(GLOB NODE_PLATFORM_SOURCES "node/platform/threadx/*.cpp" "node/platform/threadx/*.h")
    list(FILTER NODE_DESKTOP_SOURCES INCLUDE REGEX "\\.h$")     # its stand-ins, not its main()
    list(APPEND NODE_PLATFORM_SOURCES ${NODE_DESKTOP_SOURCES})
    set(NODE_PLATFORM_DIRS node/platform/threadx node/platform/desktop)
else()
    set(NODE_PLATFORM_SOURCES ${NODE_DESKTOP_SOURCES})
    set(NODE_PLATFORM_DIRS node/platform/desktop)
endif()
add_executable(sensor_node ${NODE_APP_SOURCES} ${NODE_PLATFORM_SOURCES} ${COMMON_HEADERS})
target_include_directories(sensor_node PRIVATE "${COMMON_DIR}" node/app ${NODE_PLATFORM_DIRS})
target_link_libraries(sensor_node PRIVATE sensor_dmq)
if(SENSOR_NODE_RTOS STREQUAL "THREADX")
    target_link_libraries(sensor_node PRIVATE ${THREADX_LIBRARIES})
endif()
source_group("app" FILES ${NODE_APP_SOURCES})
source_group("platform" FILES ${NODE_PLATFORM_SOURCES})
source_group("common" FILES ${COMMON_HEADERS})

# The PC client: desktop threads only
if(SENSOR_NODE_RTOS STREQUAL "NONE")
    add_executable(sensor_pc pc-client/main.cpp ${COMMON_HEADERS})
    target_include_directories(sensor_pc PRIVATE "${COMMON_DIR}")
    target_link_libraries(sensor_pc PRIVATE sensor_dmq)
    source_group("common" FILES ${COMMON_HEADERS})
endif()

# DelegateMQ's diagnostic tools (--tools): the bridges that feed dmq-spy,
# dmq-monitor and dmq-thread. Desktop builds only.
if(SENSOR_NODE_RTOS STREQUAL "NONE")
    set(TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../tools")
    set(TOOLS_BRIDGE_SOURCES
        "${TOOLS_DIR}/bridge/spy/SpyBridge.cpp"
        "${TOOLS_DIR}/bridge/node/NodeBridge.cpp")
    foreach(target sensor_node sensor_pc)
        target_sources(${target} PRIVATE ${TOOLS_BRIDGE_SOURCES})
        target_include_directories(${target} PRIVATE
            "${TOOLS_DIR}/net" "${TOOLS_DIR}/bridge/spy" "${TOOLS_DIR}/bridge/node")
    endforeach()
    source_group("tools" FILES ${TOOLS_BRIDGE_SOURCES})
endif()

foreach(target sensor_node sensor_pc)
    if(TARGET ${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE /utf-8 /W4)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra)
        endif()
    endif()
endforeach()
