# databus-freertos/client — Linux/Windows DataBus client
#
# Standard hosted build — auto-detects stdlib thread and UDP transport.
#   mkdir build && cd build
#   cmake ..
#   cmake --build .

cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_databus_freertos_client")
project(DelegateDataBusFreeRTOSClient VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# @TODO: Set build options (see Port.cmake).
# Rerun cmake configuration and clean/rebuild if you change these settings.
# set(DMQ_ASSERTS "ON")

include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

# Collect application source files
file(GLOB SOURCES "*.cpp" "*.h")

# Collect Common files to show them in the IDE
file(GLOB COMMON_SOURCES "${CMAKE_SOURCE_DIR}/../common/*.h" "${CMAKE_SOURCE_DIR}/../common/*.cpp")
set_source_files_properties(${COMMON_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

# Append DelegateMQ port/extras source files
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize sources in the IDE
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files"     FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files"   FILES ${DMQ_EXTRAS_SOURCES})
source_group("Common Files"   FILES ${COMMON_SOURCES})

include_directories(
    ${CMAKE_SOURCE_DIR}/../common
    ${DMQ_INCLUDE_DIR}
)

add_subdirectory("../common" "${CMAKE_BINARY_DIR}/common")

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES} ${COMMON_SOURCES})

target_link_libraries(${DELEGATE_APP} PRIVATE common_lib)

find_package(Threads REQUIRED)
if(WIN32)
    target_link_libraries(${DELEGATE_APP} PRIVATE ws2_32 Threads::Threads)
else()
    target_link_libraries(${DELEGATE_APP} PRIVATE Threads::Threads)
endif()
