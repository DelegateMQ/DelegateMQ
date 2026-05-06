# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_server_app")
project(Delegate VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})
add_compile_definitions(SERVER_APP)

# Disable Boost dependency for msgpack
add_compile_definitions(MSGPACK_NO_BOOST)

# Set build options (see Port.cmake)
set(DMQ_ASSERTS "OFF")
set(DMQ_ALLOCATOR "OFF")
set(DMQ_LOG "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_STDLIB")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_MSGPACK")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_ZEROMQ")
include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

if (MSVC)
    add_compile_options("/utf-8")   # Required by spdlog
    add_compile_definitions(_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
endif()

# Collect all .cpp and *.h source files in the src directory
file(GLOB SOURCES "*.cpp" "*.h")

# 1. Collect Common files to show them in VS Solution Explorer
file(GLOB COMMON_SOURCES "${CMAKE_SOURCE_DIR}/../common/*.h" "${CMAKE_SOURCE_DIR}/../common/*.cpp")

# Mark these files as "Header File Only".
# This tells CMake/VS to display them in the project tree, but NOT to compile them.
# The actual compilation happens inside 'common_lib', preventing linker errors.
set_source_files_properties(${COMMON_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

# Collect DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

# 2. Create a specific filter/folder for Common files in the IDE
source_group("Common Files" FILES ${COMMON_SOURCES})

# Add subdirectories to include path
include_directories( 
    ${CMAKE_SOURCE_DIR}/../common
    ${DMQ_INCLUDE_DIR}
    ${ZeroMQ_INCLUDE_DIR}
    ${MSGPACK_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
)

add_subdirectory("../common" "${CMAKE_BINARY_DIR}/common")

# Add an executable target
# 3. Add ${COMMON_SOURCES} to the list so they appear in the project
add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES} ${COMMON_SOURCES})

target_link_libraries(${DELEGATE_APP} PRIVATE 
    common_lib
    libzmq
)