# Cortex-M4 with FPU, Arm GNU Toolchain (arm-none-eabi-gcc, on the PATH) and
# newlib-nano. Read by ../../CMakeLists.txt before project().

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(ARM_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 --specs=nano.specs")
set(CMAKE_C_FLAGS   "${ARM_FLAGS}" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${ARM_FLAGS}" CACHE STRING "")
set(CMAKE_ASM_FLAGS "${ARM_FLAGS}" CACHE STRING "")
