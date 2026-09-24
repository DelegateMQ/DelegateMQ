cmake_minimum_required(VERSION 3.16)
project(PumptronController VERSION 1.0 LANGUAGES CXX C)

# Pumptron controller -- FreeRTOS simulator target (Windows/Linux host).
# The STM32F4 Discovery hardware target is a separate cross-compiled CMake
# project in platform/f4/ (see PUMPTRON.md).

if (NOT PUMPTRON_COMMON_DIR)
    set(PUMPTRON_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)
endif()

set(DMQ_THREAD "DMQ_THREAD_FREERTOS")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_SERIALIZE")
set(DMQ_DATABUS "ON")
set(DMQ_ALLOCATOR "ON")

include("${CMAKE_CURRENT_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")

file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS
    "${PUMPTRON_COMMON_DIR}/*.h"
    "${PUMPTRON_COMMON_DIR}/*.cpp"
)

set(APP_SOURCES
    board/IBoard.h
    pump/PumpController.cpp
    pump/PumpController.h
    pump/PumpModel.cpp
    pump/PumpModel.h
    util/Logger.cpp
    util/Logger.h
)

set(PLATFORM_SOURCES
    platform/sim/main.cpp
    platform/sim/SimBoard.h
    platform/sim/FreeRTOSConfig.h
)

add_executable(pumptron_controller
    ${APP_SOURCES}
    ${PLATFORM_SOURCES}
    ${COMMON_SOURCES}
    ${DMQ_LIB_SOURCES}
    ${DMQ_PORT_SOURCES}
    ${FREERTOS_SOURCES}
)

target_compile_definitions(pumptron_controller PRIVATE
    ${DMQ_COMPILE_DEFINITIONS}
    DMQ_USER_CONFIG="DelegateMQConfig.h"
    # The FreeRTOS Win32 port context-switches with SuspendThread, which can
    # preempt a task holding MSVC's debug-iterator _Lockit. See Cellutron.
    $<$<CXX_COMPILER_ID:MSVC>:_ITERATOR_DEBUG_LEVEL=0>
)
if(WIN32)
    target_compile_definitions(pumptron_controller PRIVATE _WIN32_WINNT=0x0601)
endif()

target_include_directories(pumptron_controller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/sim
    ${PUMPTRON_COMMON_DIR}
    ${DMQ_INCLUDE_DIR}
    ${FREERTOS_ROOT_DIR}/include
    ${FREERTOS_ROOT_DIR}/portable/${FREERTOS_PORT_DIR}
)

target_link_libraries(pumptron_controller PRIVATE ${FREERTOS_LIBRARIES})

set_target_properties(pumptron_controller PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

source_group("App Files" FILES ${APP_SOURCES})
source_group("Platform Files" FILES ${PLATFORM_SOURCES})
source_group("Common Files" FILES ${COMMON_SOURCES})
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})
source_group("FreeRTOS Files" FILES ${FREERTOS_SOURCES})
