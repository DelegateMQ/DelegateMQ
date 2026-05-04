cmake_minimum_required(VERSION 3.10)
project(DmqInterop)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define the root of the DelegateMQ library
set(DMQ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../src/delegate-mq")

# Include the DelegateMQ CMake module to get include paths and port sources
include("${DMQ_ROOT}/DelegateMQ.cmake")

# Filter out Fault.cpp from DMQ_PORT_SOURCES to allow our own FaultHandler override
list(FILTER DMQ_PORT_SOURCES EXCLUDE REGEX "Fault\\.cpp")

# Create the SHARED library (DLL on Windows)
add_library(DmqInterop SHARED 
    DmqInterop.cpp 
    DmqInterop.h
    ${DMQ_LIB_SOURCES}
    ${DMQ_PORT_SOURCES}
)

# Organize delegate source files within IDE (Visual Studio)
source_group("Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("Extras Files" FILES ${DMQ_EXTRAS_SOURCES})
source_group("Source Files" FILES DmqInterop.cpp DmqInterop.h)

# Add include directories
target_include_directories(DmqInterop PUBLIC 
    "${DMQ_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Link Mandatory Libraries
if(WIN32)
    target_link_libraries(DmqInterop PRIVATE ws2_32)
endif()

# Link ZeroMQ if selected via DMQ_TRANSPORT flag
if(DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_ZEROMQ")
    if(TARGET libzmq)
        target_link_libraries(DmqInterop PRIVATE libzmq)
    elseif(TARGET libzmq-static)
         target_link_libraries(DmqInterop PRIVATE libzmq-static)
    endif()
endif()

# Link NNG if selected
if(DMQ_TRANSPORT STREQUAL "DMQ_TRANSPORT_NNG")
    target_link_libraries(DmqInterop PRIVATE "${NNG_LIBRARY_DIR}/nng.lib")
endif()

# Set output directory to a common build folder
set_target_properties(DmqInterop PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
