# CMake toolchain file: GNU Arm Embedded (arm-none-eabi-gcc) for Cortex-M4F.
#
# Compiler search order:
#   1. ARM_TOOLCHAIN_DIR (CMake cache variable or environment variable) --
#      the directory containing arm-none-eabi-gcc(.exe)
#   2. The GCC bundled with STM32CubeIDE ("GNU Tools for STM32"), so builds
#      match the IDE's compiler exactly
#   3. arm-none-eabi-gcc on PATH (e.g. Arm GNU Toolchain)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT ARM_TOOLCHAIN_DIR AND DEFINED ENV{ARM_TOOLCHAIN_DIR})
    set(ARM_TOOLCHAIN_DIR "$ENV{ARM_TOOLCHAIN_DIR}")
endif()

if(NOT ARM_TOOLCHAIN_DIR)
    file(GLOB _cubeide_gcc
        "C:/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools-for-stm32*/tools/bin/arm-none-eabi-gcc.exe"
        "/opt/st/stm32cubeide_*/plugins/*gnu-tools-for-stm32*/tools/bin/arm-none-eabi-gcc"
        "/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/*gnu-tools-for-stm32*/tools/bin/arm-none-eabi-gcc")
    if(_cubeide_gcc)
        list(SORT _cubeide_gcc ORDER DESCENDING)   # newest plugin first
        list(GET _cubeide_gcc 0 _gcc)
        get_filename_component(ARM_TOOLCHAIN_DIR "${_gcc}" DIRECTORY)
    endif()
endif()

set(ARM_TOOLCHAIN_DIR "${ARM_TOOLCHAIN_DIR}" CACHE PATH "Directory containing arm-none-eabi-gcc")

if(ARM_TOOLCHAIN_DIR)
    set(_prefix "${ARM_TOOLCHAIN_DIR}/arm-none-eabi-")
else()
    set(_prefix "arm-none-eabi-")
endif()
if(CMAKE_HOST_WIN32)
    set(_exe ".exe")
else()
    set(_exe "")
endif()

set(CMAKE_C_COMPILER   "${_prefix}gcc${_exe}")
set(CMAKE_CXX_COMPILER "${_prefix}g++${_exe}")
set(CMAKE_ASM_COMPILER "${_prefix}gcc${_exe}")
set(CMAKE_OBJCOPY      "${_prefix}objcopy${_exe}" CACHE FILEPATH "objcopy")
set(CMAKE_SIZE         "${_prefix}size${_exe}" CACHE FILEPATH "size")

# Compiler checks can't link a hosted executable for a bare-metal target.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
