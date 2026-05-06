# databus-freertos/server — FreeRTOS DataBus server (Win32 simulator)
#
# Requires 32-bit Windows target for the FreeRTOS Windows simulator port:
#   cmake -A Win32 -B build .
#   cmake --build build

cmake_minimum_required(VERSION 3.16)

if(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
    if(NOT ${CMAKE_GENERATOR_PLATFORM} MATCHES "Win32")
        message(FATAL_ERROR "FreeRTOS Windows simulator requires 32-bit build.\nUse: cmake -A Win32 -B build .")
    endif()
endif()

set(DELEGATE_APP "delegate_databus_freertos_server")
project(DelegateDataBusFreeRTOSServer VERSION 1.0 LANGUAGES CXX C)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# Override thread to FreeRTOS; transport and serializer auto-detect from WIN32.
# On real hardware, swap DMQ_TRANSPORT to DMQ_TRANSPORT_ARM_LWIP_UDP.
set(DMQ_THREAD    "DMQ_THREAD_FREERTOS")
set(DMQ_DATABUS   "ON")

include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

# Collect application source files
file(GLOB SOURCES "*.cpp" "*.c" "*.h")

# Collect common message types
file(GLOB COMMON_SOURCES "${CMAKE_SOURCE_DIR}/../common/*.h" "${CMAKE_SOURCE_DIR}/../common/*.cpp")
set_source_files_properties(${COMMON_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

# Append DelegateMQ port/extras and FreeRTOS kernel sources
list(APPEND SOURCES ${DMQ_PORT_SOURCES})
list(APPEND SOURCES ${FREERTOS_SOURCES})

# Organize sources in the IDE
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files"     FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files"   FILES ${DMQ_EXTRAS_SOURCES})
source_group("FreeRTOS Files" FILES ${FREERTOS_SOURCES})
source_group("Common Files"   FILES ${COMMON_SOURCES})

include_directories(
    .
    ${CMAKE_SOURCE_DIR}/../common
    ${DMQ_INCLUDE_DIR}
    ${FREERTOS_ROOT_DIR}/include
    ${FREERTOS_ROOT_DIR}/portable/MSVC-MingW
)

add_subdirectory("../common" "${CMAKE_BINARY_DIR}/common")

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES} ${COMMON_SOURCES})

target_link_libraries(${DELEGATE_APP} PRIVATE common_lib)

add_definitions(-D_CRT_SECURE_NO_WARNINGS)

# Winsock required for Win32UdpTransport
if(WIN32)
    target_link_libraries(${DELEGATE_APP} PRIVATE ws2_32)
endif()
