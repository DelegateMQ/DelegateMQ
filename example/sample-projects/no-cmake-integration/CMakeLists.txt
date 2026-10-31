# Builds DelegateMQ the way a project WITHOUT CMake does (Keil, IAR,
# Makefiles, ...). CMake is used here only so build_all.py can build and run
# this sample on Windows and Linux. It deliberately does NOT include
# DelegateMQ.cmake and passes no DMQ_* options:
#
#   - DelegateMQ is one source file, src/DelegateMQ.cpp.
#   - src/delegate-mq is on the include path.
#   - Options come from config/DelegateMQConfig.h, found automatically
#     because config/ is on the include path.
#
# See "Without CMake" in docs/BUILD.md.
cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_app")
project(NoCMakeIntegration VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

set(DMQ_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../src")

add_executable(${DELEGATE_APP} main.cpp "${DMQ_SRC}/DelegateMQ.cpp")
target_compile_features(${DELEGATE_APP} PRIVATE cxx_std_17)   # C++17 or newer
target_include_directories(${DELEGATE_APP} PRIVATE "${DMQ_SRC}/delegate-mq" config)

# The default thread port (stdlib) uses std::thread.
find_package(Threads REQUIRED)
target_link_libraries(${DELEGATE_APP} PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(${DELEGATE_APP} PRIVATE /utf-8)
endif()
