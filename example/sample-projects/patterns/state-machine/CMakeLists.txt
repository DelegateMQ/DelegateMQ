# patterns/state-machine: active-fsm's StateMachine as an active object on
# DelegateMQ threads, signals and timers.
#
# Requires active-fsm, fetched by scripts/01_fetch_repos.py into
# ${DMQ_DEPS_DIR}/active-fsm (default: <DelegateMQ repo>/external/active-fsm).
# Point -DACTIVE_FSM_DIR=<path> at another checkout to use that instead.
#
# Only active-fsm's state-machine/ folder is compiled, against this
# repository's DelegateMQ.
#
#   cmake -B build .
#   cmake --build build --config Release
#   build/Release/delegate_state_machine      (Windows: build\Release\delegate_state_machine.exe)

cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_state_machine")
project(DelegateStateMachine VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DelegateMQ build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_STDLIB")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

# active-fsm (DMQ_DEPS_DIR is set by DelegateMQ.cmake)
set(ACTIVE_FSM_DIR "${DMQ_DEPS_DIR}/active-fsm" CACHE PATH "active-fsm checkout")
if(NOT EXISTS "${ACTIVE_FSM_DIR}/state-machine/StateMachine.h")
    message(FATAL_ERROR "active-fsm not found at ${ACTIVE_FSM_DIR}. "
        "Run scripts/01_fetch_repos.py, or set -DACTIVE_FSM_DIR=<path>.")
endif()
file(GLOB FSM_SOURCES "${ACTIVE_FSM_DIR}/state-machine/*.cpp" "${ACTIVE_FSM_DIR}/state-machine/*.h")

# Application sources
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/*.cpp" "${CMAKE_SOURCE_DIR}/*.h")
list(APPEND SOURCES ${FSM_SOURCES} ${DMQ_PORT_SOURCES})

# Organize source files within IDE (Visual Studio)
source_group("active-fsm" FILES ${FSM_SOURCES})
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

# StateMachine.h includes "DelegateMQ.h", found in DMQ_INCLUDE_DIR
target_include_directories(${DELEGATE_APP} PRIVATE
    .
    "${DMQ_INCLUDE_DIR}"
    "${ACTIVE_FSM_DIR}/state-machine"
)

find_package(Threads REQUIRED)
target_link_libraries(${DELEGATE_APP} PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(${DELEGATE_APP} PRIVATE /utf-8)
endif()
