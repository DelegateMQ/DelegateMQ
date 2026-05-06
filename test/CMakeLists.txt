# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_app")
project(Delegate VERSION 1.0 LANGUAGES CXX)

# @TODO: Set build options (see Port.cmake). 
# Rerun cmake configuration and clean/rebuild if you change these settings
# The DelegateMQ library auto-selects defaults based on the target platform.
# Override defaults by setting the variables below before including DelegateMQ.cmake.
# set(DMQ_ASSERTS "ON")
# set(DMQ_ALLOCATOR "ON")
# set(DMQ_LOG "ON")
# set(DMQ_DATABUS "OFF")
# set(DMQ_UTIL "OFF")
# set(DMQ_THREAD "DMQ_THREAD_NONE")
# set(DMQ_SERIALIZE "DMQ_SERIALIZE_BITSERY")
# set(DMQ_TRANSPORT "DMQ_TRANSPORT_WIN32_UDP")

# Include master delegate cmake build options
include("${CMAKE_CURRENT_SOURCE_DIR}/../src/delegate-mq/DelegateMQ.cmake")

# Collect all .cpp and *.h source files 
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

# Check if the TEST_COVERAGE argument is passed
option(TEST_COVERAGE "Enable code coverage" OFF)

if(TEST_COVERAGE)
  message(STATUS "Code coverage is enabled")

  # Add coverage flags for GCC/Clang compilers
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage -fprofile-update=atomic")
  set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fprofile-arcs -ftest-coverage -fprofile-update=atomic")
endif()

# Add subdirectories to include path
include_directories( 
    ${DMQ_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/unit-tests
)

# Add subdirectories for unit tests and sample code
# NOTE: Added before target creation so libraries are defined for linking
add_subdirectory(unit-tests)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../example/sample-code ${CMAKE_BINARY_DIR}/sample-code)

# Add an executable target
add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

# -----------------------------------------------------------------------------
# PRECOMPILED HEADERS (PCH)
# This single command works for both MSVC (Windows) and GCC/Clang (Linux).
# It compiles DelegateMQ.h once and auto-includes it into every source file
# in the target, significantly speeding up compilation.
# -----------------------------------------------------------------------------
target_precompile_headers(${DELEGATE_APP} PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/delegate-mq/DelegateMQ.h"
)

target_link_libraries(${DELEGATE_APP} PRIVATE
    unit_tests_lib
    sample_code_lib
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})