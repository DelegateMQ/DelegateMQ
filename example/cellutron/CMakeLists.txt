cmake_minimum_required(VERSION 3.16)

# Force Win32 platform for FreeRTOS Simulator compatibility on Windows
if (WIN32 AND NOT CMAKE_GENERATOR_PLATFORM)
    set(CMAKE_GENERATOR_PLATFORM Win32 CACHE STRING "Force Win32 for FreeRTOS" FORCE)
endif()

project(Cellutron VERSION 1.0 LANGUAGES CXX C)

# RTOS for the controller and safety nodes — flip with:
#   cmake -DCELLUTRON_RTOS=THREADX ..
# One switch controls both nodes; DelegateMQ isolates the application code
# from this choice (Process/System/Actuators/Sensors/state-machine sources
# are identical either way — only each node's main*.cpp kernel-init glue
# and DMQ_THREAD differ).
set(CELLUTRON_RTOS "FREERTOS" CACHE STRING "RTOS for controller/safety nodes: FREERTOS or THREADX")
set_property(CACHE CELLUTRON_RTOS PROPERTY STRINGS FREERTOS THREADX)

set(CELLUTRON_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)

# Add processor subdirectories
add_subdirectory(gui)
add_subdirectory(controller)
add_subdirectory(safety)
