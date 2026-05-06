cmake_minimum_required(VERSION 3.16)
project(CellutronController VERSION 1.0 LANGUAGES CXX C)

# If not building from the top-level Cellutron project, define variables
if (NOT CELLUTRON_COMMON_DIR)
    set(CELLUTRON_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)
endif()

# Set FreeRTOS thread port for this processor
set(DMQ_THREAD "DMQ_THREAD_FREERTOS")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_SERIALIZE")
set(DMQ_DATABUS "ON")

# Include DelegateMQ setup
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")
get_filename_component(DMQ_SRC_DIR "${DMQ_INCLUDE_DIR}" DIRECTORY)

set(TOOLS_BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/bridge")
set(TOOLS_SRC_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/src")

# Glob common sources but exclude SQLite
file(GLOB_RECURSE ALL_COMMON_SOURCES "${CELLUTRON_COMMON_DIR}/*.h" "${CELLUTRON_COMMON_DIR}/*.cpp" "${CELLUTRON_COMMON_DIR}/*.c")
set(COMMON_SOURCES ${ALL_COMMON_SOURCES})

# Ensure Network.cpp is included
list(APPEND COMMON_SOURCES "${CELLUTRON_COMMON_DIR}/util/Network.cpp")
list(APPEND COMMON_SOURCES "${CELLUTRON_COMMON_DIR}/util/RemoteConfig.cpp")
list(APPEND COMMON_SOURCES "${TOOLS_BRIDGE_DIR}/SpyBridge.cpp")
list(APPEND COMMON_SOURCES "${TOOLS_BRIDGE_DIR}/NodeBridge.cpp")
list(REMOVE_DUPLICATES COMMON_SOURCES)

foreach(FILE_PATH ${COMMON_SOURCES})
    if(FILE_PATH MATCHES "sqlite")
        list(REMOVE_ITEM COMMON_SOURCES ${FILE_PATH})
    endif()
endforeach()

# Controller application sources
set(PROCESS_FILES 
    process/Process.cpp
    process/Process.h
    process/CellProcess.cpp
    process/CellProcess.h
    process/PumpProcess.cpp
    process/PumpProcess.h
)

set(ACTUATOR_FILES 
    actuators/Actuators.cpp
    actuators/Actuators.h
    actuators/Centrifuge.cpp
    actuators/Centrifuge.h
    actuators/Pump.h
    actuators/Valve.h
)

set(SENSOR_FILES 
    sensors/Sensors.cpp
    sensors/Sensors.h
    sensors/AirSensor.h
    sensors/PressureSensor.h
    sensors/RpmSensor.h
)

set(SYSTEM_FILES 
    system/System.cpp
    system/System.h
)

add_executable(cellutron_controller 
    main.cpp 
    ${PROCESS_FILES}
    ${ACTUATOR_FILES}
    ${SENSOR_FILES}
    ${SYSTEM_FILES}
    ${COMMON_SOURCES}
    ${DMQ_LIB_SOURCES} 
    ${DMQ_PORT_SOURCES}
    ${FREERTOS_SOURCES}
)

target_compile_definitions(cellutron_controller PRIVATE ${DMQ_COMPILE_DEFINITIONS})

# Organize delegate source files within IDE (Visual Studio)
source_group("Source Files" FILES main.cpp)
source_group("Process Files" FILES ${PROCESS_FILES})
source_group("Actuator Files" FILES ${ACTUATOR_FILES})
source_group("Sensor Files" FILES ${SENSOR_FILES})
source_group("System Files" FILES ${SYSTEM_FILES})
source_group("Common Files" FILES ${COMMON_SOURCES})
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})
source_group("FreeRTOS Files" FILES ${FREERTOS_SOURCES})

target_include_directories(cellutron_controller PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CELLUTRON_COMMON_DIR}
    ${CELLUTRON_COMMON_DIR}/util
    ${CELLUTRON_COMMON_DIR}/messages
    ${DMQ_INCLUDE_DIR}
    ${DMQ_SRC_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${FREERTOS_ROOT_DIR}/include
    ${FREERTOS_ROOT_DIR}/portable/MSVC-MingW
    ${TOOLS_BRIDGE_DIR}
    ${TOOLS_SRC_DIR}
)
