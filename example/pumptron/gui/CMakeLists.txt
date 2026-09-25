cmake_minimum_required(VERSION 3.16)
project(PumptronGui VERSION 1.0 LANGUAGES CXX C)

if (NOT PUMPTRON_COMMON_DIR)
    set(PUMPTRON_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)
endif()

set(DMQ_THREAD "DMQ_THREAD_STDLIB")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_SERIALIZE")
set(DMQ_DATABUS "ON")
# DMQ_TRANSPORT stays at the platform UDP default (simulator link).
# SerialTransport.h is included explicitly by system/System.h for the
# hardware link, so both links are available at runtime.

include("${CMAKE_CURRENT_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")

set(WORKSPACE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")

# --- FTXUI ---
set(FTXUI_DIR "${WORKSPACE_DIR}/ftxui")
if(NOT EXISTS "${FTXUI_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "FTXUI not found at ${FTXUI_DIR}. Run 01_fetch_repos.py first.")
endif()
if(NOT TARGET ftxui::component)
    # FTXUI defaults to a block-only glyph fallback on Windows, which halves
    # graph resolution. Windows Terminal renders the quarter-block glyphs fine.
    set(FTXUI_MICROSOFT_TERMINAL_FALLBACK OFF CACHE BOOL "Pumptron: use full graph glyphs" FORCE)
    add_subdirectory("${FTXUI_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ftxui" EXCLUDE_FROM_ALL)
endif()

# --- libserialport ---
# Windows: built from source so it always matches this target's architecture
# (the top-level build forces Win32 for the FreeRTOS simulator, while the
# workspace's prebuilt libserialport is x64).
# Linux: uses an existing libserialport build (workspace .libs/ or the system
# libserialport-dev package). If none is found the GUI is built UDP-only
# (simulator link) and --serial is unavailable.
set(SERIALPORT_ROOT "${WORKSPACE_DIR}/libserialport")
set(PUMPTRON_HAVE_SERIAL OFF)
if(TARGET pumptron_serialport)
    set(PUMPTRON_HAVE_SERIAL ON)
elseif(WIN32)
    if(NOT EXISTS "${SERIALPORT_ROOT}/libserialport.h")
        message(FATAL_ERROR "libserialport not found at ${SERIALPORT_ROOT}. Run 01_fetch_repos.py first.")
    endif()
    add_library(pumptron_serialport SHARED
        "${SERIALPORT_ROOT}/serialport.c"
        "${SERIALPORT_ROOT}/timing.c"
        "${SERIALPORT_ROOT}/windows.c"
    )
    # LIBSERIALPORT_MSBUILD selects the MSVC config path and dllexport.
    target_compile_definitions(pumptron_serialport PRIVATE LIBSERIALPORT_MSBUILD)
    target_link_libraries(pumptron_serialport PRIVATE setupapi)
    target_include_directories(pumptron_serialport PUBLIC "${SERIALPORT_ROOT}")
    # Keep the DLL next to the executable.
    set_target_properties(pumptron_serialport PROPERTIES
        OUTPUT_NAME libserialport
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(PUMPTRON_HAVE_SERIAL ON)
else()
    find_library(SERIALPORT_LIB NAMES serialport
        PATHS "${SERIALPORT_ROOT}/.libs" "${SERIALPORT_ROOT}" NO_DEFAULT_PATH)
    if(NOT SERIALPORT_LIB)
        find_library(SERIALPORT_LIB NAMES serialport)
    endif()
    find_path(SERIALPORT_INCLUDE libserialport.h PATHS "${SERIALPORT_ROOT}")
    if(SERIALPORT_LIB AND SERIALPORT_INCLUDE)
        add_library(pumptron_serialport UNKNOWN IMPORTED)
        set_target_properties(pumptron_serialport PROPERTIES
            IMPORTED_LOCATION "${SERIALPORT_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${SERIALPORT_INCLUDE}")
        set(PUMPTRON_HAVE_SERIAL ON)
    else()
        message(WARNING "Pumptron GUI: libserialport not found -- building UDP-only (simulator link). "
                        "Install libserialport-dev or build ${SERIALPORT_ROOT} to enable --serial.")
    endif()
endif()

file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS
    "${PUMPTRON_COMMON_DIR}/*.h"
    "${PUMPTRON_COMMON_DIR}/*.cpp"
)

set(GUI_SOURCES
    main.cpp
    system/System.cpp
    system/CoreDumpFile.cpp
    system/System.h
    ui/UI.cpp
    ui/UI.h
    selftest/SelfTest.cpp
    selftest/SelfTest.h
)

add_executable(pumptron_gui
    ${GUI_SOURCES}
    ${COMMON_SOURCES}
    ${DMQ_LIB_SOURCES}
    ${DMQ_PORT_SOURCES}
)

set_target_properties(pumptron_gui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

target_compile_definitions(pumptron_gui PRIVATE ${DMQ_COMPILE_DEFINITIONS} DMQ_USER_CONFIG="DelegateMQConfig.h")
if(WIN32)
    target_compile_definitions(pumptron_gui PRIVATE _WIN32_WINNT=0x0601)
endif()

target_include_directories(pumptron_gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PUMPTRON_COMMON_DIR}
    ${DMQ_INCLUDE_DIR}
)

if(PUMPTRON_HAVE_SERIAL)
    target_compile_definitions(pumptron_gui PRIVATE PUMPTRON_HAVE_SERIAL)
    target_link_libraries(pumptron_gui PRIVATE pumptron_serialport)
endif()

target_link_libraries(pumptron_gui PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
)

source_group("GUI Files" FILES ${GUI_SOURCES})
source_group("Common Files" FILES ${COMMON_SOURCES})
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})
