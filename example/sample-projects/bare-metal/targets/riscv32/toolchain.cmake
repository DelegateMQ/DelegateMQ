# RV32IMC, xPack RISC-V Embedded GCC with newlib and libstdc++. Read by
# ../../CMakeLists.txt before project().
#
# Ubuntu's gcc-riscv64-unknown-elf package is compiler-only (no libc, no
# libstdc++), which can't link DelegateMQ (Delegate::Equal() uses
# dynamic_cast). The xPack toolchain bundles both:
# https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases
# Set its path with -DRISCV_TOOLCHAIN_PATH=<path> or the RISCV_TOOLCHAIN_PATH
# environment variable.

if(DEFINED ENV{RISCV_TOOLCHAIN_PATH})
    set(RISCV_DEFAULT_PATH "$ENV{RISCV_TOOLCHAIN_PATH}")
else()
    set(RISCV_DEFAULT_PATH "/opt/xpack-riscv-none-elf-gcc")
endif()
set(RISCV_TOOLCHAIN_PATH "${RISCV_DEFAULT_PATH}" CACHE PATH "xPack RISC-V Embedded GCC install path")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)
set(CMAKE_C_COMPILER   "${RISCV_TOOLCHAIN_PATH}/bin/riscv-none-elf-gcc")
set(CMAKE_CXX_COMPILER "${RISCV_TOOLCHAIN_PATH}/bin/riscv-none-elf-g++")
set(CMAKE_ASM_COMPILER "${RISCV_TOOLCHAIN_PATH}/bin/riscv-none-elf-gcc")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# rv32imc_zicsr/ilp32: I(base)+M(mul/div)+C(compressed), no atomics, soft
# float; a common baseline for single-core embedded RV32 parts, and (unlike
# rv32imac) one with a prebuilt libstdc++/libsupc++ in this toolchain's
# multilibs. semihost.specs: libsemihost.a provides _write/_sbrk/_exit.
set(RISCV_FLAGS "-march=rv32imc_zicsr -mabi=ilp32 --specs=semihost.specs")
set(CMAKE_C_FLAGS   "${RISCV_FLAGS}" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${RISCV_FLAGS}" CACHE STRING "")
set(CMAKE_ASM_FLAGS "${RISCV_FLAGS}" CACHE STRING "")
