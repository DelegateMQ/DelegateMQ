# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_databus_client_app")
project(DelegateDataBusClient VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# @TODO: Set build options (see Port.cmake). 
# Rerun cmake configuration and clean/rebuild if you change these settings
# The DelegateMQ library auto-selects defaults based on the target platform.
# Override defaults by setting the variables below before including DelegateMQ.cmake.
# set(DMQ_ASSERTS "ON")
# set(DMQ_DATABUS_TOOLS "OFF")

include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

# Collect all .cpp and *.h source files in this directory
file(GLOB SOURCES "*.cpp" "*.h")

# Collect Common files to show them in VS Solution Explorer
file(GLOB COMMON_SOURCES "${CMAKE_SOURCE_DIR}/../common/*.h" "${CMAKE_SOURCE_DIR}/../common/*.cpp")
set_source_files_properties(${COMMON_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

if (DMQ_DATABUS_TOOLS)
    add_compile_definitions(DMQ_DATABUS_TOOLS)
    set(SPY_BRIDGE_DIR "${CMAKE_SOURCE_DIR}/../../../../tools")
    set(SPY_BRIDGE_SOURCES
        "${SPY_BRIDGE_DIR}/bridge/SpyBridge.cpp"
        "${SPY_BRIDGE_DIR}/bridge/SpyBridge.h"
    )
    include_directories(
        "${SPY_BRIDGE_DIR}/src"
        "${SPY_BRIDGE_DIR}/bridge"
    )
    list(APPEND SOURCES ${SPY_BRIDGE_SOURCES})
endif()

# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})
source_group("Common Files" FILES ${COMMON_SOURCES})

# Add subdirectories to include path
include_directories( 
    ${CMAKE_SOURCE_DIR}/../common
    ${DMQ_INCLUDE_DIR}
)

add_subdirectory("../common" "${CMAKE_BINARY_DIR}/common")

# Add an executable target
add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES} ${COMMON_SOURCES})

target_link_libraries(${DELEGATE_APP} PRIVATE 
    common_lib
)

# Link libraries based on platform
find_package(Threads REQUIRED)
if(WIN32)
    target_link_libraries(${DELEGATE_APP} PRIVATE ws2_32 Threads::Threads)
else()
    target_link_libraries(${DELEGATE_APP} PRIVATE Threads::Threads)
endif()
