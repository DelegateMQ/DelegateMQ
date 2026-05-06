# CMake command must use 32-bit architecture on Windows FreeRTOS simulation build.
# cmake -A Win32 -B build .

# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.16)

if (CMAKE_HOST_SYSTEM_NAME MATCHES "Windows") 
    if (${CMAKE_GENERATOR_PLATFORM} MATCHES "Win32")
        message(STATUS "32-bit architecture (Win32) set.")
    else()
        message(FATAL_ERROR "64-bit architecture not supported on FreeRTOS build.\nUse `-A Win32` cmake option.")
    endif()
else()
    message(WARNING "Unknown architecture. Ensure 32-bit for FreeRTOS build.")
endif()

set(DELEGATE_APP "delegate_app")
project(Delegate VERSION 1.0 LANGUAGES CXX C)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# Set build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_FREERTOS")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")

# Collect all .cpp and *.h source files in the src directory
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/*.c*" "${CMAKE_SOURCE_DIR}/*.h")

# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Collect FreeRTOS source files
list(APPEND SOURCES ${FREERTOS_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})
source_group("FreeRTOS Files" FILES ${FREERTOS_SOURCES})

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

