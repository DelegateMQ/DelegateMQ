# DelegateMQ on FreeRTOS, two ways to build the same tests (main_delegate.cpp):
#
#   FreeRTOS Win32 simulator (default; Windows, 32-bit only):
#     cmake -A Win32 -B build .
#
#   A real Cortex-M CPU on QEMU: -DBM_TARGET=<name>, a folder in targets/:
#     arm-m3   Cortex-M3, arm-none-eabi-gcc, FreeRTOS GCC/ARM_CM3, QEMU mps2-an385
#     cmake -B build-arm-m3 -G Ninja -DBM_TARGET=arm-m3
#
# targets/<name>/ has toolchain.cmake (compilers, flags, FreeRTOS port; read
# before project()) and target.cmake (startup code, linker script, bsp.c).

# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.16)

set(BM_TARGET "" CACHE STRING "Cortex-M target: a folder in targets/ (arm-m3); empty for the Win32 simulator")

if(BM_TARGET)
    set(BM_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/targets/${BM_TARGET}")
    if(NOT EXISTS "${BM_TARGET_DIR}/toolchain.cmake")
        message(FATAL_ERROR "Unknown BM_TARGET '${BM_TARGET}': no targets/${BM_TARGET}/toolchain.cmake")
    endif()
    # Cross compiler, flags and FreeRTOS port: must be set before project()
    include("${BM_TARGET_DIR}/toolchain.cmake")
elseif (CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
    if (${CMAKE_GENERATOR_PLATFORM} MATCHES "Win32")
        message(STATUS "32-bit architecture (Win32) set.")
    else()
        message(FATAL_ERROR "64-bit architecture not supported on FreeRTOS build.\nUse `-A Win32` cmake option.")
    endif()
else()
    message(WARNING "Unknown architecture. Ensure 32-bit for FreeRTOS build.")
endif()

set(DELEGATE_APP "delegate_app")
if(BM_TARGET)
    project(Delegate VERSION 1.0 LANGUAGES CXX C ASM)
else()
    project(Delegate VERSION 1.0 LANGUAGES CXX C)
endif()
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# Set build options. The Cortex-M targets use the fixed-block allocator, in
# static pools (targets/<name>/target.cmake): Test 14 checks that nothing uses
# the heap once the system is running.
if(BM_TARGET)
    set(DMQ_ALLOCATOR "ON")
else()
    set(DMQ_ALLOCATOR "OFF")
endif()
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_FREERTOS")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})
source_group("FreeRTOS Files" FILES ${FREERTOS_SOURCES})

if(BM_TARGET)
    # The tests, DelegateMQ and the FreeRTOS kernel; the target adds startup
    # code, FreeRTOS hooks and the timer interrupt
    add_executable(${DELEGATE_APP} main_delegate.cpp
        ${DMQ_PORT_SOURCES} ${FREERTOS_SOURCES} ${DMQ_LIB_SOURCES})
    target_include_directories(${DELEGATE_APP} PRIVATE
        .
        ${DMQ_INCLUDE_DIR}
        ${FREERTOS_ROOT_DIR}/include
        ${FREERTOS_ROOT_DIR}/portable/${FREERTOS_PORT_DIR}
    )
    target_compile_features(${DELEGATE_APP} PRIVATE cxx_std_17)
    include("${BM_TARGET_DIR}/target.cmake")
    return()
endif()

# Win32 simulator: every source file in this folder (main.c is the
# simulator's entry point)
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/*.c*" "${CMAKE_SOURCE_DIR}/*.h")

# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Collect FreeRTOS source files
list(APPEND SOURCES ${FREERTOS_SOURCES})

# Add subdirectories to include path
include_directories(
    .
    ${DMQ_INCLUDE_DIR}
    ${FREERTOS_ROOT_DIR}/include
    ${FREERTOS_ROOT_DIR}/portable/MSVC-MingW
)

# Add an executable target
add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

add_definitions(-D_CRT_SECURE_NO_WARNINGS)
