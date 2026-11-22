# patterns/async-sqlite: SQLite made thread-safe by running it on its own
# DelegateMQ thread, with Async-SQLite.
#
# Requires Async-SQLite, fetched by scripts/01_fetch_repos.py into
# ${DMQ_DEPS_DIR}/Async-SQLite (default: <DelegateMQ repo>/external/Async-SQLite).
# Point -DASYNC_SQLITE_DIR=<path> at another checkout to use that instead.
#
# Only Async-SQLite's SQLite/ folder is compiled (the SQLite amalgamation and
# async_sqlite3.cpp), against this repository's DelegateMQ.
#
#   cmake -B build .
#   cmake --build build --config Release
#   build/Release/delegate_async_sqlite      (Windows: build\Release\delegate_async_sqlite.exe)

cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_async_sqlite")
project(DelegateAsyncSqlite VERSION 1.0 LANGUAGES C CXX)
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

# Async-SQLite (DMQ_DEPS_DIR is set by DelegateMQ.cmake)
set(ASYNC_SQLITE_DIR "${DMQ_DEPS_DIR}/Async-SQLite" CACHE PATH "Async-SQLite checkout")
if(NOT EXISTS "${ASYNC_SQLITE_DIR}/SQLite/async_sqlite3.h")
    message(FATAL_ERROR "Async-SQLite not found at ${ASYNC_SQLITE_DIR}. "
        "Run scripts/01_fetch_repos.py, or set -DASYNC_SQLITE_DIR=<path>.")
endif()
set(SQLITE_DIR "${ASYNC_SQLITE_DIR}/SQLite")
set(SQLITE_SOURCES
    "${SQLITE_DIR}/sqlite3.c"
    "${SQLITE_DIR}/sqlite3.h"
    "${SQLITE_DIR}/async_sqlite3.cpp"
    "${SQLITE_DIR}/async_sqlite3.h"
)

# SQLite's own C source: build it without warnings
if(MSVC)
    set_source_files_properties("${SQLITE_DIR}/sqlite3.c" PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties("${SQLITE_DIR}/sqlite3.c" PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Application sources
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/*.cpp" "${CMAKE_SOURCE_DIR}/*.h")
list(APPEND SOURCES ${SQLITE_SOURCES} ${DMQ_PORT_SOURCES})

# Organize source files within IDE (Visual Studio)
source_group("Async-SQLite" FILES ${SQLITE_SOURCES})
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

target_include_directories(${DELEGATE_APP} PRIVATE
    .
    "${DMQ_INCLUDE_DIR}"
    "${SQLITE_DIR}"
)

find_package(Threads REQUIRED)
target_link_libraries(${DELEGATE_APP} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(MSVC)
    target_compile_options(${DELEGATE_APP} PRIVATE /utf-8 /bigobj)
endif()
