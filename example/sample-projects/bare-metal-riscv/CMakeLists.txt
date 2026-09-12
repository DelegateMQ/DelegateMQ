cmake_minimum_required(VERSION 3.16)

# Path to the xPack RISC-V Embedded GCC toolchain (bundles a full newlib +
# libstdc++/libsupc++ for riscv-none-elf -- Ubuntu's own gcc-riscv64-unknown-elf
# apt package is compiler-only, no libc/libstdc++ at all, which won't link
# DelegateMQ's dynamic_cast-based Delegate::Equal()).
# Download: https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases
# Override with: cmake -DRISCV_TOOLCHAIN_PATH=<path> ...
set(RISCV_TOOLCHAIN_PATH "/opt/xpack-riscv-none-elf-gcc" CACHE PATH "xPack RISC-V Embedded GCC install path")

# --- RISC-V CROSS-COMPILATION SETUP ---
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)
set(CMAKE_C_COMPILER   "${RISCV_TOOLCHAIN_PATH}/bin/riscv-none-elf-gcc")
set(CMAKE_CXX_COMPILER "${RISCV_TOOLCHAIN_PATH}/bin/riscv-none-elf-g++")
set(CMAKE_ASM_COMPILER "${RISCV_TOOLCHAIN_PATH}/bin/riscv-none-elf-gcc")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# rv32imc_zicsr/ilp32: I(base)+M(mul/div)+C(compressed), no atomics/float -- a
# common baseline for real single-core embedded RV32 parts, and (unlike
# rv32imac) has a complete prebuilt libstdc++/libsupc++ in this toolchain's
# multilib set. Soft-float ABI (ilp32, no 'f'/'d').
set(RISCV_FLAGS "-march=rv32imc_zicsr -mabi=ilp32 --specs=semihost.specs")
set(CMAKE_C_FLAGS   "${RISCV_FLAGS}" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${RISCV_FLAGS}" CACHE STRING "")
set(CMAKE_ASM_FLAGS  "${RISCV_FLAGS}" CACHE STRING "")
# ---------------------------------------------------------------------------

set(DELEGATE_APP "delegate_app")
project(Delegate VERSION 1.0 LANGUAGES C CXX ASM)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# Options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_NONE")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")

include("${CMAKE_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

include_directories(${DMQ_INCLUDE_DIR})

# startup.S supplies _start (see its own header comment for why the xPack
# toolchain's generic crt0.o can't be used for a direct -bios none boot);
# trap_entry.S supplies the machine-mode trap vector this sample installs
# into mtvec at runtime. Neither retarget.c nor the toolchain's own crt0.o
# is used -- libsemihost.a (via --specs=semihost.specs above) still handles
# printf/_sbrk/etc. through QEMU semihosting.
#
# NOTE: DMQ_EXTRAS_SOURCES (not the full DMQ_PORT_SOURCES) -- DMQ_PORT_SOURCES
# also includes port/fault/Fault.cpp, which would collide at link time with
# this file's own FaultHandler()/WatchdogHandler() definitions (same reason
# as bare-metal-arm/CMakeLists.txt).
add_executable(${DELEGATE_APP} main.cpp startup.S trap_entry.S ${DMQ_LIB_SOURCES} ${DMQ_EXTRAS_SOURCES})

target_compile_features(${DELEGATE_APP} PRIVATE cxx_std_20)

# NOTE: -march/-mabi/--specs are already in CMAKE_CXX_FLAGS above, which
# CMake also passes to the link step (driven through the C++ compiler) --
# repeating --specs=semihost.specs here would apply its "%rename lib" spec
# directive twice in one invocation, which GCC rejects outright.
target_link_options(${DELEGATE_APP} PRIVATE
    "-nostartfiles" # this sample's own startup.S provides _start, not crt0.o
    "-T${CMAKE_SOURCE_DIR}/virt.ld"
    "-Wl,-Map=${CMAKE_BINARY_DIR}/output.map"
)
