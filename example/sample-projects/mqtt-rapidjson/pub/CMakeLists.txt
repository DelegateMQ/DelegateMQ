# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_pub_app") 
project(Delegate VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# Set build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_STDLIB")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_RAPIDJSON")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_MQTT")
include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

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
    ${MQTT_INCLUDE_DIR}
    ${RAPIDJSON_INCLUDE_DIR}
)

# Add an executable target
add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

# -------------------------------------------------------------------------
# Find Libraries (UPDATED)
# -------------------------------------------------------------------------
# Note: MQTT_LIBRARY_DIR is set in External.cmake
# We add PATH_SUFFIXES to look in Debug/Release subfolders
# We add NAMES to look for 'paho-mqtt3cd' (debug) and 'paho-mqtt3c' (release)

find_library(MQTT_LIB_A 
    NAMES paho-mqtt3a paho-mqtt3ad
    HINTS ${MQTT_LIBRARY_DIR}
    PATH_SUFFIXES Debug Release
)

find_library(MQTT_LIB_C 
    NAMES paho-mqtt3c paho-mqtt3cd
    HINTS ${MQTT_LIBRARY_DIR}
    PATH_SUFFIXES Debug Release
)

# Check if libraries were found
if(NOT MQTT_LIB_A OR NOT MQTT_LIB_C)
    message(FATAL_ERROR "Could not find MQTT libraries in ${MQTT_LIBRARY_DIR}. \
    Checked subdirectories: Debug, Release. \
    Checked names: paho-mqtt3c, paho-mqtt3cd")
endif()

# Link the libraries
target_link_libraries(${DELEGATE_APP} PRIVATE 
    ${MQTT_LIB_A}
    ${MQTT_LIB_C}
)

# =========================================================
# Copy DLLs post-build (WINDOWS ONLY)
# =========================================================
if(WIN32)
    add_custom_command(TARGET ${DELEGATE_APP} POST_BUILD
        # 1. Copy and RENAME the C library
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MQTT_BINARY_DIR}/$<CONFIG>/paho-mqtt3c$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:${DELEGATE_APP}>/paho-mqtt3c.dll" 
        
        # 2. Copy and RENAME the Async library
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MQTT_BINARY_DIR}/$<CONFIG>/paho-mqtt3a$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:${DELEGATE_APP}>/paho-mqtt3a.dll"

        COMMENT "Copying and renaming Paho MQTT DLLs to output directory"
    )
endif()