# Cortex-M3 (Armv7-M), soft float, Arm Toolchain for Embedded (ATfE): clang,
# picolibc, libc++, lld. Read by ../../CMakeLists.txt before project().
#
# ATfE: https://github.com/arm/arm-toolchain/releases (ATfE-<version>-<host>).
# Set its path with -DATFE_PATH=<path> or the ATFE_PATH environment variable.

if(DEFINED ENV{ATFE_PATH})
    set(ATFE_DEFAULT_PATH "$ENV{ATFE_PATH}")
else()
    set(ATFE_DEFAULT_PATH "C:/ATfE-22.1.0-Windows-x86_64")
endif()
set(ATFE_PATH "${ATFE_DEFAULT_PATH}" CACHE PATH "Arm Toolchain for Embedded install path")

# Host executable suffix (CMAKE_EXECUTABLE_SUFFIX isn't set before project())
if(CMAKE_HOST_WIN32)
    set(ATFE_EXE ".exe")
else()
    set(ATFE_EXE "")
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER   "${ATFE_PATH}/bin/clang${ATFE_EXE}")
set(CMAKE_CXX_COMPILER "${ATFE_PATH}/bin/clang++${ATFE_EXE}")
set(CMAKE_ASM_COMPILER "${ATFE_PATH}/bin/clang${ATFE_EXE}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# These flags select ATfE's armv7m_soft_nofp_exn_rtti_size runtime (exceptions
# and RTTI, which DelegateMQ needs, optimized for size)
set(ARM_FLAGS "--target=thumbv7m-unknown-none-eabi -mfpu=none -mno-unaligned-access --sysroot=${ATFE_PATH}/lib/clang-runtimes")
set(CMAKE_C_FLAGS   "${ARM_FLAGS}" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${ARM_FLAGS}" CACHE STRING "")
set(CMAKE_ASM_FLAGS "${ARM_FLAGS}" CACHE STRING "")
