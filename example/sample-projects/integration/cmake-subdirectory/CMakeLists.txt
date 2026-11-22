# integration/cmake-subdirectory — DelegateMQ added to an application the way
# docs/BUILD.md ("With CMake") tells users to: FetchContent, then link the
# DelegateMQ::DelegateMQ target. Nothing else from DelegateMQ is used: no
# DelegateMQ.cmake include, no source lists.
#
# FetchContent points at this checkout (SOURCE_DIR) instead of GitHub, so the
# build tests the code here, offline. An application would use
# GIT_REPOSITORY/GIT_TAG instead, or add_subdirectory() on a clone.
#
#   cmake -B build .
#   cmake --build build
#   ./build/delegate_cmake_subdirectory

cmake_minimum_required(VERSION 3.16)
set(DELEGATE_APP "delegate_cmake_subdirectory")
project(DelegateCMakeSubdirectory VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DMQ_* options before DelegateMQ is added, as BUILD.md shows. The rest are
# the defaults: stdlib threads, the built-in serializer, UDP.
set(DMQ_ALLOCATOR "ON")

include(FetchContent)
FetchContent_Declare(
    delegatemq
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../.."
)
FetchContent_MakeAvailable(delegatemq)

add_executable(${DELEGATE_APP} main.cpp)
target_link_libraries(${DELEGATE_APP} PRIVATE DelegateMQ::DelegateMQ)

# BUILD.md: "Nothing is added to your own directory's compile definitions."
# This target doesn't link DelegateMQ, so it must not see DMQ_ALLOCATOR
# (not_linked.cpp stops the build if it does).
add_library(not_linked OBJECT not_linked.cpp)
target_link_libraries(${DELEGATE_APP} PRIVATE not_linked)

# BUILD.md: "Only the DelegateMQ static library is built. The tests, samples
# and tools are skipped."
foreach(_target delegate_app dmq-spy dmq-monitor dmq-thread dmq-target)
    if(TARGET ${_target})
        message(FATAL_ERROR "Adding DelegateMQ created '${_target}'; only the library should be built")
    endif()
endforeach()
