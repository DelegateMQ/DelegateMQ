# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_databus_interop_server")
project(DataBusInteropServer VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# @TODO: Set build options (see Port.cmake). 
# Rerun cmake configuration and clean/rebuild if you change these settings
# The DelegateMQ library auto-selects defaults based on the target platform.
# Override defaults by setting the variables below before including DelegateMQ.cmake.
set(DMQ_SERIALIZE "DMQ_SERIALIZE_MSGPACK")

include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

if (MSVC)
    add_compile_options("/utf-8")
    add_compile_definitions(_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
endif()

# Collect source files
file(GLOB SOURCES "*.cpp" "*.h")
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize in IDE
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

# Include paths
include_directories(
    ${DMQ_INCLUDE_DIR}
    ${MSGPACK_INCLUDE_DIR}
)

# Executable
add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

# Precompiled header
target_precompile_headers(${DELEGATE_APP} PRIVATE
    "${DMQ_INCLUDE_DIR}/DelegateMQ.h"
)

# Link libraries
find_package(Threads REQUIRED)
if (WIN32)
    target_link_libraries(${DELEGATE_APP} PRIVATE ws2_32 Threads::Threads)
else()
    target_link_libraries(${DELEGATE_APP} PRIVATE Threads::Threads)
endif()
