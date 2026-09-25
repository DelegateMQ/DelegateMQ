cmake_minimum_required(VERSION 3.20)

# Pumptron controller firmware -- STM32F4 Discovery (STM32F407VG), FreeRTOS.
#
# From example/pumptron:
#   cmake -S controller/platform/f4 -B build/f4 -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/f4
#
# Produces pumptron_f4.elf / .hex / .bin. Flash with STM32CubeIDE (import or
# debug the .elf), STM32CubeProgrammer, or OpenOCD. See PUMPTRON.md.
#
# Inputs:
#   STM32_REPO         STM32Cube FW_F4 package (HAL, CMSIS, Discovery BSP)
#   ARM_TOOLCHAIN_DIR  optional, see arm-none-eabi-gcc.cmake

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/arm-none-eabi-gcc.cmake")
endif()

project(PumptronF4 C CXX ASM)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- Paths ---
set(PUMPTRON_CONTROLLER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(PUMPTRON_COMMON_DIR     "${PUMPTRON_CONTROLLER_DIR}/../common")
set(DMQ_REPO_DIR            "${PUMPTRON_CONTROLLER_DIR}/../../..")

if(DEFINED ENV{USERPROFILE})
    set(_home "$ENV{USERPROFILE}")
else()
    set(_home "$ENV{HOME}")
endif()
set(STM32_REPO "${_home}/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3" CACHE PATH
    "STM32Cube FW_F4 firmware package (contains Drivers/)")
if(NOT EXISTS "${STM32_REPO}/Drivers/STM32F4xx_HAL_Driver")
    message(FATAL_ERROR "STM32Cube FW_F4 package not found at '${STM32_REPO}'. "
                        "Install it via STM32CubeIDE/CubeMX or pass -DSTM32_REPO=<path>.")
endif()

# --- MCU flags ---
set(MCU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
add_compile_options(
    ${MCU_FLAGS}
    -ffunction-sections -fdata-sections
    -Wall
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:ASM>:-x$<SEMICOLON>assembler-with-cpp>
)
set(CMAKE_C_FLAGS_DEBUG     "-Og -g3")
set(CMAKE_CXX_FLAGS_DEBUG   "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE   "-Os -g")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g")
set(CMAKE_C_STANDARD 11)

add_compile_definitions(STM32F407xx USE_HAL_DRIVER USE_STM32F4_DISCO)

# --- DelegateMQ ---
set(DMQ_THREAD     "DMQ_THREAD_FREERTOS")
set(DMQ_SERIALIZE  "DMQ_SERIALIZE_SERIALIZE")
set(DMQ_TRANSPORT  "DMQ_TRANSPORT_STM32_UART")
set(DMQ_DATABUS    "ON")
set(DMQ_ALLOCATOR  "ON")
set(DMQ_ASSERTS    "ON")
set(DMQ_UTIL       "ON")
set(FREERTOS_PORT_DIR "GCC/ARM_CM4F")
set(FREERTOS_HEAP     "heap_4")
include("${DMQ_REPO_DIR}/src/delegate-mq/DelegateMQ.cmake")

# --- STM32Cube HAL + Discovery BSP ---
file(GLOB HAL_SOURCES "${STM32_REPO}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal*.c")
list(FILTER HAL_SOURCES EXCLUDE REGEX ".*_template\\.c$")

set(BSP_DIR "${STM32_REPO}/Drivers/BSP")
set(BSP_SOURCES
    "${BSP_DIR}/STM32F4-Discovery/stm32f4_discovery.c"
    "${BSP_DIR}/STM32F4-Discovery/stm32f4_discovery_accelerometer.c"
    "${BSP_DIR}/Components/lis3dsh/lis3dsh.c"
    "${BSP_DIR}/Components/lis302dl/lis302dl.c"
)

# --- Application ---
file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS "${PUMPTRON_COMMON_DIR}/*.cpp")

set(APP_SOURCES
    "${PUMPTRON_CONTROLLER_DIR}/pump/PumpController.cpp"
    "${PUMPTRON_CONTROLLER_DIR}/pump/PumpModel.cpp"
    "${PUMPTRON_CONTROLLER_DIR}/util/Logger.cpp"
)

set(PLATFORM_SOURCES
    main.cpp
    F4Board.cpp
    Fault.cpp
    CoreDump.cpp
    stm32f4xx_it.c
    syscalls.c
    system_stm32f4xx.c
    startup_stm32f407vgtx.s
)

# Only the .c/.cpp files from DelegateMQ's source lists need compiling.
set(DMQ_COMPILED_SOURCES ${DMQ_LIB_SOURCES} ${DMQ_PORT_SOURCES})
list(FILTER DMQ_COMPILED_SOURCES INCLUDE REGEX ".*\\.(c|cpp)$")
# Replace the library's fault handler with Pumptron's LED-signalling one (Fault.cpp here).
list(FILTER DMQ_COMPILED_SOURCES EXCLUDE REGEX ".*/port/fault/Fault\\.cpp$")
set(FREERTOS_COMPILED_SOURCES ${FREERTOS_SOURCES})
list(FILTER FREERTOS_COMPILED_SOURCES INCLUDE REGEX ".*\\.c$")

add_executable(pumptron_f4
    ${PLATFORM_SOURCES}
    ${APP_SOURCES}
    ${COMMON_SOURCES}
    ${DMQ_COMPILED_SOURCES}
    ${FREERTOS_COMPILED_SOURCES}
    ${HAL_SOURCES}
    ${BSP_SOURCES}
)
set_target_properties(pumptron_f4 PROPERTIES SUFFIX ".elf")

target_compile_definitions(pumptron_f4 PRIVATE DMQ_USER_CONFIG="DelegateMQConfig.h")

target_include_directories(pumptron_f4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}                          # FreeRTOSConfig.h, stm32f4xx_hal_conf.h, F4Board.h
    ${PUMPTRON_CONTROLLER_DIR}                           # DelegateMQConfig.h, board/, pump/
    ${PUMPTRON_COMMON_DIR}
    ${DMQ_INCLUDE_DIR}
    ${FREERTOS_ROOT_DIR}/include
    ${FREERTOS_ROOT_DIR}/portable/${FREERTOS_PORT_DIR}
    ${STM32_REPO}/Drivers/CMSIS/Include
    ${STM32_REPO}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${STM32_REPO}/Drivers/STM32F4xx_HAL_Driver/Inc
    ${BSP_DIR}/STM32F4-Discovery
    ${BSP_DIR}/Components/lis3dsh
    ${BSP_DIR}/Components/lis302dl
)

target_link_options(pumptron_f4 PRIVATE
    ${MCU_FLAGS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F407VGTX_FLASH.ld
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--build-id=sha1     # read by CoreDump.cpp; placed in ROM by the linker script
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/pumptron_f4.map,--cref
    -Wl,--print-memory-usage
)
target_link_libraries(pumptron_f4 PRIVATE m)

add_custom_command(TARGET pumptron_f4 POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:pumptron_f4> ${CMAKE_CURRENT_BINARY_DIR}/pumptron_f4.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:pumptron_f4> ${CMAKE_CURRENT_BINARY_DIR}/pumptron_f4.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:pumptron_f4>
    COMMENT "Creating pumptron_f4.hex / .bin"
)
