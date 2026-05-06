cmake_minimum_required(VERSION 3.16)
project(CellutronGui VERSION 1.0 LANGUAGES CXX C)

if (NOT CELLUTRON_COMMON_DIR)
    set(CELLUTRON_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)
endif()

# Set standard thread port for GUI
set(DMQ_THREAD "DMQ_THREAD_STDLIB")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_SERIALIZE")
set(DMQ_DATABUS "ON")

# Include DelegateMQ setup
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")
get_filename_component(DMQ_SRC_DIR "${DMQ_INCLUDE_DIR}" DIRECTORY)

set(TOOLS_BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/bridge")
set(TOOLS_SRC_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/src")

# Paths for FTXUI
set(WORKSPACE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
set(FTXUI_DIR "${WORKSPACE_DIR}/ftxui")

# --- FTXUI ---
if(EXISTS "${FTXUI_DIR}/CMakeLists.txt")
    add_subdirectory("${FTXUI_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ftxui" EXCLUDE_FROM_ALL)
else()
    message(FATAL_ERROR "FTXUI not found at ${FTXUI_DIR}. Run 01_fetch_repos.py first.")
endif()

file(GLOB_RECURSE ALL_COMMON_SOURCES "${CELLUTRON_COMMON_DIR}/*.h" "${CELLUTRON_COMMON_DIR}/*.cpp" "${CELLUTRON_COMMON_DIR}/*.c")
set(COMMON_SOURCES ${ALL_COMMON_SOURCES})

# Ensure Network.cpp and RemoteConfig.cpp are included
list(APPEND COMMON_SOURCES "${CELLUTRON_COMMON_DIR}/util/Network.cpp")
list(APPEND COMMON_SOURCES "${CELLUTRON_COMMON_DIR}/util/RemoteConfig.cpp")
list(APPEND COMMON_SOURCES "${TOOLS_BRIDGE_DIR}/SpyBridge.cpp")
list(APPEND COMMON_SOURCES "${TOOLS_BRIDGE_DIR}/NodeBridge.cpp")
list(REMOVE_DUPLICATES COMMON_SOURCES)

set(UI_FILES 
    ui/UI.cpp
    ui/UI.h
)

set(LOG_FILES 
    logs/Logs.cpp
    logs/Logs.h
)

set(ALARM_FILES 
    alarms/Alarms.cpp
    alarms/Alarms.h
)

set(SYSTEM_FILES 
    system/System.cpp
    system/System.h
)

add_executable(cellutron_gui 
    main.cpp 
    ${UI_FILES}
    ${LOG_FILES}
    ${ALARM_FILES}
    ${SYSTEM_FILES}
    ${COMMON_SOURCES}
    ${DMQ_LIB_SOURCES} 
    ${DMQ_PORT_SOURCES}
)

target_compile_definitions(cellutron_gui PRIVATE ${DMQ_COMPILE_DEFINITIONS})
if(WIN32)
    target_compile_definitions(cellutron_gui PRIVATE _WIN32_WINNT=0x0601)
endif()

# Organize delegate source files within IDE (Visual Studio)
source_group("Source Files" FILES main.cpp)
source_group("UI Files" FILES ${UI_FILES})
source_group("Log Files" FILES ${LOG_FILES})
source_group("Alarm Files" FILES ${ALARM_FILES})
source_group("System Files" FILES ${SYSTEM_FILES})
source_group("Common Files" FILES ${COMMON_SOURCES})
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

# Include directories
target_include_directories(cellutron_gui PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/logs
    ${CMAKE_CURRENT_SOURCE_DIR}/alarms
    ${CELLUTRON_COMMON_DIR}
    ${CELLUTRON_COMMON_DIR}/util
    ${CELLUTRON_COMMON_DIR}/messages
    ${DMQ_INCLUDE_DIR}
    ${DMQ_SRC_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${TOOLS_BRIDGE_DIR}
    ${TOOLS_SRC_DIR}
)

target_link_libraries(cellutron_gui PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
)
