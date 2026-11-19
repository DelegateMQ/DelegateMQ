# Cortex-M0 (Armv6-M), Arm GNU Toolchain (arm-none-eabi-gcc, on the PATH) and
# newlib-nano. Read by ../../CMakeLists.txt before project().
#
# Armv6-M has no exclusive-access instructions (LDREX/STREX), so an atomic
# read-modify-write compiles to a libatomic call this toolchain can't link,
# and unaligned loads/stores fault. Building and running here catches both.
#
# Built without RTTI (-fno-rtti), as many small-MCU projects are: DelegateMQ
# then defines DMQ_NO_RTTI itself and needs no dynamic_cast or typeid.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(ARM_FLAGS "-mcpu=cortex-m0 -mthumb -mfloat-abi=soft --specs=nano.specs")
set(CMAKE_C_FLAGS   "${ARM_FLAGS}" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${ARM_FLAGS} -fno-rtti" CACHE STRING "")
set(CMAKE_ASM_FLAGS "${ARM_FLAGS}" CACHE STRING "")
