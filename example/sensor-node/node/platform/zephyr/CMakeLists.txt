# sensor-node on Zephyr: the node/app code on Zephyr's native_sim board (a
# Linux process running the real Zephyr kernel), DataBus over Zephyr's UDP
# sockets, offloaded to the host's, so the desktop sensor_pc talks to it.
#
#   cmake -B build -DBOARD=native_sim/native/64 .     (ZEPHYR_BASE set; or west build)
#   ./build/zephyr/zephyr.exe -testargs 10            (run time in s; 0 = forever)
#
# Zephyr owns the executable target ("app"): this file adds sources to it.

cmake_minimum_required(VERSION 3.20.0)

if(NOT (UNIX AND NOT APPLE))
    message(FATAL_ERROR "sensor-node's Zephyr build uses native_sim: Linux only.")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(SensorNodeZephyr)

set(SENSOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

set(DMQ_ALLOCATOR "OFF")
set(DMQ_THREAD    "DMQ_THREAD_ZEPHYR")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_SERIALIZE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_ZEPHYR_UDP")
set(DMQ_DATABUS   "ON")
include("${SENSOR_ROOT}/../../src/delegate-mq/DelegateMQ.cmake")

file(GLOB NODE_APP_SOURCES "${SENSOR_ROOT}/node/app/*.cpp")
target_sources(app PRIVATE main.cpp ${NODE_APP_SOURCES} ${DMQ_LIB_SOURCES} ${DMQ_PORT_SOURCES})
target_include_directories(app PRIVATE ${DMQ_INCLUDE_DIR} .
    "${SENSOR_ROOT}/node/app" "${SENSOR_ROOT}/common")
# A thread's stack is 2 KB by default; NetThread serializes, sends and prints
target_compile_definitions(app PRIVATE DMQ_ZEPHYR_STACK_BYTES=16384)
