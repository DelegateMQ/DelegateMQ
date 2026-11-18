# Cortex-M3 (Armv7-M), Arm GNU Toolchain (arm-none-eabi-gcc, on the PATH) and
# newlib-nano. Read by ../../CMakeLists.txt before project().

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(ARM_FLAGS "-mcpu=cortex-m3 -mthumb -mfloat-abi=soft --specs=nano.specs")
set(CMAKE_C_FLAGS   "${ARM_FLAGS}" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${ARM_FLAGS}" CACHE STRING "")
set(CMAKE_ASM_FLAGS "${ARM_FLAGS}" CACHE STRING "")

# FreeRTOS kernel port and heap, read by DelegateMQ's External.cmake
set(FREERTOS_PORT_DIR "GCC/ARM_CM3")
set(FREERTOS_HEAP "heap_4")
