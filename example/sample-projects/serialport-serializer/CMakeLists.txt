# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_app")
project(Delegate VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})
add_compile_definitions(SERVER_APP)

# Set build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_STDLIB")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_SERIALIZE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_SERIAL_PORT")
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
    ${SERIALPORT_INCLUDE_DIR}
)

link_directories(
    ${SERIALPORT_LIBRARY_DIR}
)

# Add an executable target
add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

target_link_libraries(${DELEGATE_APP} libserialport)

add_custom_command(TARGET ${DELEGATE_APP} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SERIALPORT_ROOT}/x64/Debug/libserialport.dll"      # Source
    $<TARGET_FILE_DIR:${DELEGATE_APP}>                    # Destination (EXE folder)
)

