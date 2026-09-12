# cmsis-rtos2-linux — DelegateMQ feature demo on CMSIS-RTOS2, built on Zephyr's
# own CMSIS-RTOS v2 compatibility layer (CONFIG_CMSIS_RTOS_V2) for the
# native_sim simulation port.
#
# This verifies dmq::os::CmsisRtos2Thread/Mutex/Semaphore/DelegateQueue
# against a real CMSIS-RTOS2 implementation -- Zephyr's own
# subsys/portability/cmsis_rtos_v2/, not a stub -- without any ARM
# cross-toolchain, QEMU, or real hardware. It does NOT verify
# CmsisRtos2CriticalSection: that type drops to ARM Cortex-M CMSIS-Core
# intrinsics (__disable_irq()/__get_PRIMASK()/__set_PRIMASK()) which have no
# meaning on native_sim's x86-64 host process. See CLAUDE.md's "ISR-Safe
# Locking" section.
#
# Requires a west workspace with Zephyr checked out (NOT the plain sibling
# clone that ThreadX/FreeRTOS use -- see README.md for setup). Manual-only:
# not wired into 01_fetch_repos.py/03_generate_samples.py/04_build_samples.py,
# since it needs `west build`, not a plain `cmake -B build`.
#
# Zephyr owns the executable target itself (created by find_package(Zephyr)
# below, named "app") -- unlike every other sample here, this CMakeLists.txt
# does not call add_executable(); it adds sources/includes to that target.
#
#   west build -b native_sim/native/64 . -d build
#   ./build/zephyr/zephyr.exe

cmake_minimum_required(VERSION 3.20.0)

if(NOT (UNIX AND NOT APPLE))
    message(FATAL_ERROR "cmsis-rtos2-linux only builds on Linux (Zephyr native_sim port).")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(DelegateCmsisRtos2Linux)

# Set build options
set(DMQ_ALLOCATOR "OFF")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_CMSIS_RTOS2")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/../../../src/delegate-mq/DelegateMQ.cmake")

# Organize delegate source files within IDE (Visual Studio)
source_group("DelegateMQ/Delegate Files" FILES ${DMQ_LIB_SOURCES})
source_group("DelegateMQ/Port Files" FILES ${DMQ_PORT_ONLY_SOURCES})
source_group("DelegateMQ/Extras Files" FILES ${DMQ_EXTRAS_SOURCES})

target_sources(app PRIVATE main_delegate.cpp DelegateThreadsTests.cpp TimerDelegateTests.cpp ${DMQ_LIB_SOURCES} ${DMQ_PORT_SOURCES})

# native_sim_stubs/cmsis_compiler.h is a sample-local, no-op stand-in for the
# real ARM CMSIS-Core header (see that file's comment) -- listed first so it
# resolves before anything else on the include path could provide the name.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/native_sim_stubs ${DMQ_INCLUDE_DIR} ${ZEPHYR_BASE}/include/zephyr/portability)
