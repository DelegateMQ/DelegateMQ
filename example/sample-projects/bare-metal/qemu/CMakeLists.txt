# DelegateMQ bare-metal sample: one set of tests (main.cpp), built for the
# target selected with -DBM_TARGET=<name> (a folder in targets/):
#
#   arm-m4       Cortex-M4, arm-none-eabi-gcc + newlib, QEMU mps2-an386 (default)
#   arm-m3-atfe  Cortex-M3, Arm Toolchain for Embedded (clang, picolibc, libc++),
#                QEMU mps2-an385
#   riscv32      RV32IMC, xPack RISC-V Embedded GCC + newlib, QEMU virt
#
#   cmake -B build-arm-m4 -G Ninja -DBM_TARGET=arm-m4
#   cmake --build build-arm-m4
#
# Each targets/<name>/ has toolchain.cmake (compilers and flags, read before
# project()) and target.cmake (startup code, linker script, bsp.cpp). See
# README.md.

cmake_minimum_required(VERSION 3.16)

set(BM_TARGET "arm-m4" CACHE STRING "Target: a folder in targets/ (arm-m4, arm-m0, arm-m33, arm-m3-atfe, riscv32)")
set(BM_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/targets/${BM_TARGET}")
if(NOT EXISTS "${BM_TARGET_DIR}/toolchain.cmake")
    message(FATAL_ERROR "Unknown BM_TARGET '${BM_TARGET}': no targets/${BM_TARGET}/toolchain.cmake")
endif()

# Cross compiler and flags: must be set before project()
include("${BM_TARGET_DIR}/toolchain.cmake")

set(DELEGATE_APP "delegate_app")
project(Delegate VERSION 1.0 LANGUAGES C CXX ASM)

# Options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_NONE")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")

include("${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

# DMQ_EXTRAS_SOURCES (Timer), not the full DMQ_PORT_SOURCES: that also has
# port/fault/Fault.cpp, which would collide at link time with main.cpp's own
# FaultHandler()/WatchdogHandler().
add_executable(${DELEGATE_APP} main.cpp ${DMQ_LIB_SOURCES} ${DMQ_EXTRAS_SOURCES})
target_include_directories(${DELEGATE_APP} PRIVATE ${DMQ_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(${DELEGATE_APP} PRIVATE cxx_std_17)

# Startup code, linker script, bsp.cpp and link options of the target
include("${BM_TARGET_DIR}/target.cmake")
