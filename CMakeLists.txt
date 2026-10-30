# Example CMake command line to create project build files:
#
# *** Windows ***
# cmake -G "Visual Studio 17 2022" -A Win32 -B build -S .
# cmake -G "Visual Studio 17 2022" -A x64 -B build -S .
#
# *** Linux ***
# cmake -G "Unix Makefiles" -B build -S .
# cmake -G "Unix Makefiles" -B build -S . -DTEST_COVERAGE=ON

# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.16)

# Project name and language (C or C++)
project(DelegateProject VERSION 2.2.0 LANGUAGES CXX)

option(DMQ_FORCE_OPTIMIZE_DEBUG "Force compiler optimizations on DelegateMQ templates even in Debug builds" OFF)

# Consumed by another project via add_subdirectory() or FetchContent: build only
# the DelegateMQ::DelegateMQ library target, configured by any DMQ_* variables
# the parent set beforehand. The tests, samples and tools below are skipped.
if(NOT CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(DMQ_NO_GLOBAL_DEFINITIONS ON)
    include("${PROJECT_SOURCE_DIR}/src/delegate-mq/DelegateMQ.cmake")
    dmq_add_library(DelegateMQ)
    add_library(DelegateMQ::DelegateMQ ALIAS DelegateMQ)
    return()
endif()

# Add subdirectory for building delegate_app
add_subdirectory(test ${CMAKE_BINARY_DIR}/delegate_app)

# Optional: build diagnostic tools (dmq-spy, dmq-monitor, dmq-target)
# Requires FTXUI, spdlog, and bitsery in the workspace.
# Enable with: cmake -DDMQ_TOOLS=ON ...
option(DMQ_TOOLS "Build DelegateMQ diagnostic tools (dmq-spy, dmq-monitor)" OFF)
if(DMQ_TOOLS)
    add_subdirectory(tools ${CMAKE_BINARY_DIR}/dmq-tools)
endif()
