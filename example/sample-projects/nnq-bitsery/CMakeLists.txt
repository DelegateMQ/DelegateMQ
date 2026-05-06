# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_app")
project(Delegate VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

if (MSVC)
    add_compile_options("/Zc:__cplusplus")
endif()

# Set build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_STDLIB")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_BITSERY")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NNG")
include("${CMAKE_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")

# Collect all .cpp and *.h source files in the src directory
file(GLOB SOURCES "*.cpp" "*.h")

# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

# Add subdirectories to include path
include_directories( 
    ${DMQ_INCLUDE_DIR}
    ${NNG_INCLUDE_DIR}
    ${BITSERY_INCLUDE_DIR}
)

# Add an executable target
add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

find_library(NNG_LIB nng HINTS ${NNG_LIBRARY_DIR})

if (NOT NNG_LIB)
    message(FATAL_ERROR "Could not find nng.lib in ${NNG_LIBRARY_DIR}")
endif()

add_definitions(-DNNG_STATIC_LIB) 

target_link_libraries(${DELEGATE_APP} PRIVATE 
    ${NNG_LIB}
)

# Link ws2_32 on Windows only
if (WIN32)
    target_link_libraries(${DELEGATE_APP} PRIVATE ws2_32)
endif()


