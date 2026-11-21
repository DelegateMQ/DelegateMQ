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

# DelegateMQ: the desktop defaults (stdlib threads, the built-in serializer,
# UDP) plus the DataBus. One library, shared by both programs.
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

# The node: the portable app (node/app) on the desktop platform
file(GLOB NODE_APP_SOURCES "node/app/*.cpp" "node/app/*.h")
file(GLOB NODE_DESKTOP_SOURCES "node/platform/desktop/*.cpp" "node/platform/desktop/*.h")
add_executable(sensor_node ${NODE_APP_SOURCES} ${NODE_DESKTOP_SOURCES} ${COMMON_HEADERS})
target_include_directories(sensor_node PRIVATE "${COMMON_DIR}" node/app node/platform/desktop)
target_link_libraries(sensor_node PRIVATE sensor_dmq)
source_group("app" FILES ${NODE_APP_SOURCES})
source_group("platform/desktop" FILES ${NODE_DESKTOP_SOURCES})
source_group("common" FILES ${COMMON_HEADERS})

# The PC client
add_executable(sensor_pc pc-client/main.cpp ${COMMON_HEADERS})
target_include_directories(sensor_pc PRIVATE "${COMMON_DIR}")
target_link_libraries(sensor_pc PRIVATE sensor_dmq)
source_group("common" FILES ${COMMON_HEADERS})

if(MSVC)
    target_compile_options(sensor_node PRIVATE /utf-8 /W4)
    target_compile_options(sensor_pc PRIVATE /utf-8 /W4)
else()
    target_compile_options(sensor_node PRIVATE -Wall -Wextra)
    target_compile_options(sensor_pc PRIVATE -Wall -Wextra)
endif()
