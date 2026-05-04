cmake_minimum_required(VERSION 3.10)
project(InteropServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DMQ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../src/delegate-mq")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_MSGPACK")
include("${DMQ_ROOT}/DelegateMQ.cmake")

add_executable(InteropServer main.cpp ${DMQ_PORT_SOURCES} ${DMQ_LIB_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("Extras Files" FILES ${DMQ_EXTRAS_SOURCES})
source_group("Source Files" FILES main.cpp)

target_include_directories(InteropServer PUBLIC 
    "${DMQ_ROOT}"
    "${MSGPACK_INCLUDE_DIR}"
)

if(WIN32)
    target_link_libraries(InteropServer PRIVATE ws2_32)
endif()
