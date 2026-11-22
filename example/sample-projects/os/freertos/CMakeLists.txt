# os/freertos — DelegateMQ feature demo on FreeRTOS (POSIX/Linux simulation port)
#
# Requires the FreeRTOS kernel fetched by 01_fetch_repos.py at
# ${DMQ_DEPS_DIR}/FreeRTOS (default: <DelegateMQ repo>/external/FreeRTOS).
#
# Linux only: uses the FreeRTOS ThirdParty/GCC/Posix simulator port, which
# implements each FreeRTOS task as a real pthread.
#
#   mkdir build && cd build
#   cmake ..
#   cmake --build .

cmake_minimum_required(VERSION 3.16)

if(NOT (UNIX AND NOT APPLE))
    message(FATAL_ERROR "os/freertos only builds on Linux (FreeRTOS POSIX simulator port).")
endif()

set(DELEGATE_APP "delegate_freertos_linux")
project(DelegateFreeRTOSLinux VERSION 1.0 LANGUAGES CXX C)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# Set build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_FREERTOS")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

# Collect application source files
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/*.c*" "${CMAKE_SOURCE_DIR}/*.h")

# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# The portable tests every OS port sample runs (test/portable)
include("${CMAKE_SOURCE_DIR}/../../../../test/portable/PortableTests.cmake")
list(APPEND SOURCES ${PORTABLE_TESTS_SOURCES})

# Collect FreeRTOS source files (kernel + POSIX port, set by External.cmake)
list(APPEND SOURCES ${FREERTOS_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})
source_group("FreeRTOS Files" FILES ${FREERTOS_SOURCES})

include_directories(
    .
    ${DMQ_INCLUDE_DIR}
    ${PORTABLE_TESTS_DIR}
    ${FREERTOS_ROOT_DIR}/include
    ${FREERTOS_ROOT_DIR}/portable/${FREERTOS_PORT_DIR}
)

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

target_link_libraries(${DELEGATE_APP} PRIVATE ${FREERTOS_LIBRARIES})
