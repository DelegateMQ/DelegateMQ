cmake_minimum_required(VERSION 3.16)

# Path to the Arm Toolchain for Embedded (ATfE).
# Override with: cmake -DATFE_PATH=<path> ...
set(ATFE_PATH "C:/ATfE-22.1.0-Windows-x86_64" CACHE PATH "Arm Toolchain for Embedded install path")

# Armv7-M, soft float, no FPU, exceptions+RTTI — matches armv7m_soft_nofp_exn_rtti_size
set(RUNTIME_DIR "${ATFE_PATH}/lib/clang-runtimes/arm-none-eabi/armv7m_soft_nofp_exn_rtti_size")

# --- ATfE CROSS-COMPILATION SETUP ---
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER   "${ATFE_PATH}/bin/clang.exe")
set(CMAKE_CXX_COMPILER "${ATFE_PATH}/bin/clang++.exe")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Target flags that select the armv7m_soft_nofp_exn_rtti_size multilib variant
set(ARM_FLAGS "--target=thumbv7m-unknown-none-eabi -mfpu=none -mno-unaligned-access --sysroot=${ATFE_PATH}/lib/clang-runtimes")
set(CMAKE_C_FLAGS   "${ARM_FLAGS}" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${ARM_FLAGS}" CACHE STRING "")
# ---------------------------------------------------------------------------

set(DELEGATE_APP "delegate_app")
project(Delegate VERSION 1.0 LANGUAGES C CXX)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DELEGATE_APP})

# DelegateMQ options — bare-metal, no threading, no serialization, no transport
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_NONE")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")

include("${CMAKE_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")
list(APPEND SOURCES ${DMQ_PORT_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

include_directories(${DMQ_INCLUDE_DIR})

# No startup.c or retarget.c needed:
#   crt0-semihost.o  — vector table, Reset_Handler, C++ static constructor init
#   libsemihost.a    — routes printf/scanf to QEMU semihosting output
add_executable(${DELEGATE_APP} main.cpp ${DMQ_LIB_SOURCES})

target_compile_features(${DELEGATE_APP} PRIVATE cxx_std_17)

# Memory layout for QEMU mps2-an385 (Cortex-M3):
#   boot_flash — 4 KB  @ 0x00000000  (vector table + _start)
#   flash      — 4092 KB @ 0x00001000  (code + rodata)
#   ram        — 4 MB  @ 0x20000000
target_link_options(${DELEGATE_APP} PRIVATE
    "--target=thumbv7m-unknown-none-eabi"
    "-fuse-ld=${ATFE_PATH}/bin/ld.lld.exe"
    "-nostartfiles"       # prevent sysroot from auto-linking crt0.o; we supply crt0-semihost.o explicitly
    "-Wl,--defsym=__boot_flash=0x00000000"
    "-Wl,--defsym=__boot_flash_size=0x00001000"
    "-Wl,--defsym=__flash=0x00001000"
    "-Wl,--defsym=__flash_size=0x003FF000"
    "-Wl,--defsym=__ram=0x20000000"
    "-Wl,--defsym=__ram_size=0x00400000"
    "-T${RUNTIME_DIR}/lib/picolibcpp.ld"
    "-Wl,-Map=${CMAKE_BINARY_DIR}/output.map"
    "${RUNTIME_DIR}/lib/crt0-semihost.o"
    "-L${RUNTIME_DIR}/lib"
    "-lc++"
    "-lc++abi"
    "-lunwind"
    "-lsemihost"
    "-lc"
    "-lm"
    "-lclang_rt.builtins"
)
