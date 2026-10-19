# DepsDir.cmake
# Resolves DMQ_DEPS_DIR: the directory holding the third-party repos fetched by
# 01_fetch_repos.py (zeromq, nng, mqtt, msgpack-c, FreeRTOS, ftxui, ...).
#
# Order:
#   1. -DDMQ_DEPS_DIR=<path> on the cmake command line
#   2. DMQ_DEPS_DIR environment variable
#   3. <DelegateMQ repo>/external (the default, gitignored)
#
# Included by DelegateMQ.cmake. Build files that need a dependency before
# including DelegateMQ.cmake can include this file directly.
#
# No include_guard: CMake variables are directory-scoped, so a project that
# includes DelegateMQ.cmake from several subdirectories needs DMQ_DEPS_DIR set
# in each of them.

if(NOT DMQ_DEPS_DIR AND DEFINED ENV{DMQ_DEPS_DIR})
    set(DMQ_DEPS_DIR "$ENV{DMQ_DEPS_DIR}")
endif()

if(NOT DMQ_DEPS_DIR)
    set(DMQ_DEPS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../external")
endif()

get_filename_component(DMQ_DEPS_DIR "${DMQ_DEPS_DIR}" ABSOLUTE)
