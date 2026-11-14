# qt-linux — DelegateMQ feature demo on Qt threads (DMQ_THREAD_QT)
#
# Needs Qt 6 Core only (no GUI modules). On Debian/Ubuntu:
#   sudo apt install qt6-base-dev
#
#   cmake -B build .
#   cmake --build build
#   ./build/delegate_qt_linux
#
# Elsewhere, point CMake at a Qt install: -DCMAKE_PREFIX_PATH=<Qt>/6.x.x/gcc_64

cmake_minimum_required(VERSION 3.16)

set(DELEGATE_APP "delegate_qt_linux")
project(DelegateQtLinux VERSION 1.0 LANGUAGES CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# Set build options. DMQ_THREAD_QT makes DelegateMQ.cmake find Qt6::Core and
# turn on CMAKE_AUTOMOC (QtThread.h declares Q_OBJECT classes).
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_QT")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")

find_package(Threads REQUIRED)

# Collect application source files
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/*.c*" "${CMAKE_SOURCE_DIR}/*.h")

# Collect DelegateMQ port/extras source files (includes QtThread.h for moc)
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize delegate source files within IDE
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

include_directories(
    .
    ${DMQ_INCLUDE_DIR}
)

add_executable(${DELEGATE_APP} ${SOURCES} ${DMQ_LIB_SOURCES})

target_link_libraries(${DELEGATE_APP} PRIVATE Qt6::Core Threads::Threads)
