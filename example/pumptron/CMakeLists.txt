cmake_minimum_required(VERSION 3.16)

# Pumptron desktop build: the FTXUI operator console (gui) and the pump
# controller running on the FreeRTOS simulator (controller).
#
# The STM32F4 Discovery firmware is a separate cross-compiled project:
#   cmake -S controller/platform/f4 -B build/f4 -G Ninja
# See PUMPTRON.md.

# The FreeRTOS Win32 simulator port is 32-bit only.
if (WIN32 AND NOT CMAKE_GENERATOR_PLATFORM)
    set(CMAKE_GENERATOR_PLATFORM Win32 CACHE STRING "Force Win32 for FreeRTOS" FORCE)
endif()

project(Pumptron VERSION 1.0 LANGUAGES CXX C)

set(PUMPTRON_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)

add_subdirectory(gui)
add_subdirectory(controller)
