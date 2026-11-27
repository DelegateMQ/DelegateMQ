# DelegateMQ on the NUCLEO-H755ZI-Q (STM32H755ZI: Cortex-M7 at 400 MHz and
# Cortex-M4 at 200 MHz). One build makes both images:
#
#   cmake -B build -G Ninja
#   cmake --build build
#   python run.py            (flash both cores, check the output on the ST-LINK VCP)
#
# build/h755_cm7.elf  flash bank 1 (0x08000000): FreeRTOS and the DelegateMQ
#                     test suite of os/freertos-bare-metal
# build/h755_cm4.elf  flash bank 2 (0x08100000): waits for the Cortex-M7's
#                     release, then idles (the shared-memory link comes next)
#
# The STM32Cube H7 package (HAL, CMSIS) comes from STM32CUBE_H7_DIR, by default
# where STM32CubeMX downloads it. FreeRTOS is the example workspace's
# (external/FreeRTOS), the same kernel as the other FreeRTOS samples.

cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-none-eabi.cmake" CACHE FILEPATH "")
project(DelegateMQ_H755 LANGUAGES C CXX ASM)

if(DEFINED ENV{STM32CUBE_H7_DIR})
    set(_cube_default "$ENV{STM32CUBE_H7_DIR}")
else()
    if(DEFINED ENV{USERPROFILE})
        set(_home "$ENV{USERPROFILE}")
    else()
        set(_home "$ENV{HOME}")
    endif()
    # V1.12.1 (built and checked with), else the newest FW_H7 STM32CubeMX installed
    file(TO_CMAKE_PATH "${_home}/STM32Cube/Repository" _cube_repo)
    set(_cube_default "${_cube_repo}/STM32Cube_FW_H7_V1.12.1")
    if(NOT EXISTS "${_cube_default}")
        file(GLOB _cube_found LIST_DIRECTORIES true "${_cube_repo}/STM32Cube_FW_H7_V*")
        if(_cube_found)
            list(SORT _cube_found COMPARE NATURAL ORDER DESCENDING)
            list(GET _cube_found 0 _cube_default)
        endif()
    endif()
endif()
set(STM32CUBE_H7_DIR "${_cube_default}" CACHE PATH "STM32Cube FW_H7 package (HAL, CMSIS)")
if(NOT EXISTS "${STM32CUBE_H7_DIR}/Drivers/STM32H7xx_HAL_Driver")
    message(FATAL_ERROR "STM32Cube FW_H7 not found at '${STM32CUBE_H7_DIR}'. Install it with "
        "STM32CubeMX (Help > Manage embedded software packages) or set STM32CUBE_H7_DIR.")
endif()

set(SUITE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../os/freertos-bare-metal")

# --------------------------------------------------------------------------
# DelegateMQ: FreeRTOS port, fixed-block allocator in static pools (the suite's
# Test 14 checks that nothing uses the heap once the system runs)
# --------------------------------------------------------------------------
set(DMQ_ALLOCATOR "ON")
set(DMQ_UTIL "ON")
set(DMQ_THREAD "DMQ_THREAD_FREERTOS")
set(DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set(DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
# The H7's Cortex-M7 is r1p1 or later: FreeRTOS's ARM_CM7/r0p1 port is only
# for r0p1 silicon, so both cores use ARM_CM4F
set(FREERTOS_PORT_DIR "GCC/ARM_CM4F")
set(FREERTOS_HEAP "heap_4")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/delegate-mq/DelegateMQ.cmake")

# --------------------------------------------------------------------------
# STM32H7 HAL and CMSIS, shared by both cores
# --------------------------------------------------------------------------
set(HAL_DIR "${STM32CUBE_H7_DIR}/Drivers/STM32H7xx_HAL_Driver")
set(CMSIS_DEV_DIR "${STM32CUBE_H7_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx")
set(HAL_SOURCES "")
foreach(_m hal hal_cortex hal_rcc hal_rcc_ex hal_gpio hal_pwr hal_pwr_ex hal_hsem
           hal_uart hal_uart_ex hal_dma hal_dma_ex hal_mdma hal_exti hal_flash hal_flash_ex)
    list(APPEND HAL_SOURCES "${HAL_DIR}/Src/stm32h7xx_${_m}.c")
endforeach()

set(BOARD_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${SUITE_DIR}"
    "${HAL_DIR}/Inc"
    "${CMSIS_DEV_DIR}/Include"
    "${STM32CUBE_H7_DIR}/Drivers/CMSIS/Include"
    ${DMQ_INCLUDE_DIR}
    ${DMQ_EXTERNAL_INCLUDE_DIRS}
)

# Supply: the board is wired for the internal SMPS. A build that configures
# the LDO instead leaves the chip unreachable until a BOOT0 recovery, so this
# define (read by ExitRun0Mode() in the startup code) must stay.
# USER_VECT_TAB_ADDRESS: SystemInit() points VTOR at this core's flash bank,
# not only the boot address option bytes.
set(BOARD_DEFINES STM32H755xx USE_HAL_DRIVER USE_PWR_DIRECT_SMPS_SUPPLY USER_VECT_TAB_ADDRESS)

set(COMMON_FLAGS -mthumb -mfloat-abi=hard -ffunction-sections -fdata-sections -Wall)
set(CXX_FLAGS -fno-exceptions -fno-rtti -fno-threadsafe-statics)

function(h755_image target core)
    if(core STREQUAL "CM7")
        set(cpu -mcpu=cortex-m7 -mfpu=fpv5-d16)
        set(ld "${CMAKE_CURRENT_SOURCE_DIR}/cm7/stm32h755_cm7.ld")
    else()
        set(cpu -mcpu=cortex-m4 -mfpu=fpv4-sp-d16)
        set(ld "${CMAKE_CURRENT_SOURCE_DIR}/cm4/stm32h755_cm4.ld")
    endif()
    target_sources(${target} PRIVATE
        ${HAL_SOURCES}
        "${CMSIS_DEV_DIR}/Source/Templates/system_stm32h7xx_dualcore_boot_cm4_cm7.c"
        "${CMSIS_DEV_DIR}/Source/Templates/gcc/startup_stm32h755xx.s"
        "${CMAKE_CURRENT_SOURCE_DIR}/common/syscalls.c"
    )
    target_include_directories(${target} PRIVATE ${BOARD_INCLUDES})
    target_compile_definitions(${target} PRIVATE ${BOARD_DEFINES} CORE_${core})
    target_compile_options(${target} PRIVATE ${cpu} ${COMMON_FLAGS}
        $<$<COMPILE_LANGUAGE:CXX>:${CXX_FLAGS}>
        $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_link_options(${target} PRIVATE ${cpu} -mthumb -mfloat-abi=hard
        "-T${ld}" --specs=nano.specs
        -Wl,--gc-sections "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${target}.map"
        -Wl,--print-memory-usage)
    target_link_libraries(${target} PRIVATE c m)
    set_target_properties(${target} PROPERTIES SUFFIX ".elf" LINK_DEPENDS "${ld}")
endfunction()

# --------------------------------------------------------------------------
# Cortex-M7: FreeRTOS and the DelegateMQ test suite
# --------------------------------------------------------------------------
add_executable(h755_cm7
    cm7/main.c
    cm7/bsp.c
    cm7/hooks.c
    cm7/fault.cpp
    cm7/stm32h7xx_it.c
    cm7/transport_check.cpp
    "${SUITE_DIR}/main_delegate.cpp"
    ${DMQ_LIB_SOURCES}
    ${DMQ_PORT_SOURCES}
    ${FREERTOS_SOURCES}
)
h755_image(h755_cm7 CM7)
# Before the suite folder: this core's FreeRTOSConfig.h, not the suite's
target_include_directories(h755_cm7 BEFORE PRIVATE cm7)
target_compile_definitions(h755_cm7 PRIVATE BSP_TARGET DMQ_CUSTOM_FAULT_HANDLER
    # DMQ_ALLOCATOR in static pools: the same counts as the suite's Cortex-M3
    # target (os/freertos-bare-metal/targets/arm-m3/target.cmake)
    DMQ_XALLOCATOR_STATIC_POOLS
    DMQ_XALLOC_POOL_16=8
    DMQ_XALLOC_POOL_32=48
    DMQ_XALLOC_POOL_64=12
    DMQ_XALLOC_POOL_128=4
    DMQ_XALLOC_POOL_512=2
)
# Every malloc, calloc and realloc (C++ new included) goes through hooks.c's
# counting wrappers (BspHeapCallCount()), for the suite's Test 14
target_link_options(h755_cm7 PRIVATE
    "-Wl,--wrap=malloc" "-Wl,--wrap=calloc" "-Wl,--wrap=realloc")

# --------------------------------------------------------------------------
# Cortex-M4: boot handshake only (no RTOS yet)
# --------------------------------------------------------------------------
add_executable(h755_cm4
    cm4/main.c
    cm4/stm32h7xx_it.c
)
h755_image(h755_cm4 CM4)
target_include_directories(h755_cm4 BEFORE PRIVATE cm4)
