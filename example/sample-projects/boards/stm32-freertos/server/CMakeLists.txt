cmake_minimum_required(VERSION 3.20)

# The STM32F4 Discovery firmware (server), the same build as the STM32CubeIDE
# project in STM32CubeIDE/ but without the IDE:
#
#   cmake -B build -G Ninja -DSTM32_REPO=<STM32Cube FW_F4 package>
#   cmake --build build
#
# Produces stm32_freertos_server.elf / .hex / .bin. Flash with
# STM32CubeProgrammer, OpenOCD, or debug the .elf from STM32CubeIDE. The test
# mode (RUN_SIMPLE_TESTS, RUN_NETWORK_TESTS, ...) is chosen in Src/main.cpp, as
# for the IDE build.
#
# Same sources as the IDE project: Src/, ../common, its FreeRTOS copy
# (STM32CubeIDE/Middlewares/FreeRTOS), startup code, syscalls and linker
# script, and from the STM32Cube FW_F4 package the HAL, the Discovery BSP and
# the USB device library (HID).
#
# Inputs:
#   STM32_REPO         STM32Cube FW_F4 package (default: the newest under
#                      ~/STM32Cube/Repository, where STM32CubeMX/IDE install it)
#   ARM_TOOLCHAIN_DIR  optional, see arm-none-eabi-gcc.cmake

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/arm-none-eabi-gcc.cmake")
endif()

project(Stm32FreertosServer C CXX ASM)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- Paths ---
set(SAMPLE_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(IDE_DIR      "${CMAKE_CURRENT_SOURCE_DIR}/STM32CubeIDE")
set(DMQ_REPO_DIR "${SAMPLE_DIR}/../../../..")

if(NOT STM32_REPO)
    if(DEFINED ENV{STM32_REPO})
        set(_repo "$ENV{STM32_REPO}")
    else()
        if(DEFINED ENV{USERPROFILE})
            set(_home "$ENV{USERPROFILE}")
        else()
            set(_home "$ENV{HOME}")
        endif()
        # Folders only: the repository also holds the packages' .zip files
        # (and Windows globbing ignores case)
        file(GLOB _repos LIST_DIRECTORIES true "${_home}/STM32Cube/Repository/STM32Cube_FW_F4_V*")
        list(SORT _repos COMPARE NATURAL ORDER DESCENDING)
        foreach(_r ${_repos})
            if(IS_DIRECTORY "${_r}/Drivers/STM32F4xx_HAL_Driver")
                set(_repo "${_r}")
                break()
            endif()
        endforeach()
    endif()
    set(STM32_REPO "${_repo}" CACHE PATH "STM32Cube FW_F4 firmware package (contains Drivers/)")
endif()
if(NOT EXISTS "${STM32_REPO}/Drivers/STM32F4xx_HAL_Driver")
    message(FATAL_ERROR "STM32Cube FW_F4 package not found at '${STM32_REPO}'. "
                        "Install it with STM32CubeIDE/CubeMX or pass -DSTM32_REPO=<path>.")
endif()

# --- MCU and compiler flags (as the IDE project) ---
set(MCU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
add_compile_options(
    ${MCU_FLAGS}
    -ffunction-sections -fdata-sections
    -Wall
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:ASM>:-x$<SEMICOLON>assembler-with-cpp>
)
set(CMAKE_C_FLAGS_DEBUG     "-O0 -g3")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE   "-Os -g")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g")
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

add_compile_definitions(STM32F407xx USE_HAL_DRIVER USE_STM32F4_DISCO SERVER_APP
                        DMQ_MAX_TIMER_EXPIRED=8 $<$<CONFIG:Debug>:DEBUG>)

# --- DelegateMQ ---
set(DMQ_THREAD     "DMQ_THREAD_FREERTOS")
set(DMQ_SERIALIZE  "DMQ_SERIALIZE_SERIALIZE")
set(DMQ_TRANSPORT  "DMQ_TRANSPORT_STM32_UART")
set(DMQ_ALLOCATOR  "ON")
set(DMQ_ASSERTS    "ON")
set(DMQ_UTIL       "ON")
set(FREERTOS_PORT_DIR "GCC/ARM_CM4F")   # unused: the sample's own FreeRTOS copy is built below
include("${DMQ_REPO_DIR}/src/delegate-mq/DelegateMQ.cmake")

# Only the .c/.cpp files from DelegateMQ's source lists need compiling
set(DMQ_COMPILED_SOURCES ${DMQ_LIB_SOURCES} ${DMQ_PORT_SOURCES})
list(FILTER DMQ_COMPILED_SOURCES INCLUDE REGEX ".*\\.(c|cpp)$")

# --- FreeRTOS: the IDE project's copy (V10.3.1, Cortex-M4F port, heap_4) ---
set(RTOS_DIR "${IDE_DIR}/Middlewares/FreeRTOS/Source")
file(GLOB RTOS_SOURCES "${RTOS_DIR}/*.c")
list(APPEND RTOS_SOURCES "${RTOS_DIR}/portable/port.c" "${RTOS_DIR}/portable/MemMang/heap_4.c")

# --- STM32Cube FW_F4: HAL, Discovery BSP, USB device library (HID) ---
set(HAL_DIR "${STM32_REPO}/Drivers/STM32F4xx_HAL_Driver")
file(GLOB HAL_SOURCES "${HAL_DIR}/Src/stm32f4xx_hal*.c")
list(FILTER HAL_SOURCES EXCLUDE REGEX ".*_template\\.c$")
list(APPEND HAL_SOURCES
    "${HAL_DIR}/Src/stm32f4xx_ll_fsmc.c"
    "${HAL_DIR}/Src/stm32f4xx_ll_sdmmc.c"
    "${HAL_DIR}/Src/stm32f4xx_ll_usb.c"
)
set(BSP_DIR "${STM32_REPO}/Drivers/BSP")
set(BSP_SOURCES
    "${BSP_DIR}/STM32F4-Discovery/stm32f4_discovery.c"
    "${BSP_DIR}/STM32F4-Discovery/stm32f4_discovery_accelerometer.c"
    "${BSP_DIR}/Components/lis3dsh/lis3dsh.c"
    "${BSP_DIR}/Components/lis302dl/lis302dl.c"
)
set(USB_DIR "${STM32_REPO}/Middlewares/ST/STM32_USB_Device_Library")
set(USB_SOURCES
    "${USB_DIR}/Core/Src/usbd_core.c"
    "${USB_DIR}/Core/Src/usbd_ctlreq.c"
    "${USB_DIR}/Core/Src/usbd_ioreq.c"
    "${USB_DIR}/Class/HID/Src/usbd_hid.c"
)

# --- Application ---
file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cpp")
file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS "${SAMPLE_DIR}/common/*.cpp")
set(STARTUP_SOURCES
    "${IDE_DIR}/Application/Startup/startup_stm32f407vgtx.s"
    "${IDE_DIR}/Application/User/syscalls.c"
    "${IDE_DIR}/Application/User/sysmem.c"
)

add_executable(stm32_freertos_server
    ${APP_SOURCES}
    ${COMMON_SOURCES}
    ${STARTUP_SOURCES}
    ${DMQ_COMPILED_SOURCES}
    ${RTOS_SOURCES}
    ${HAL_SOURCES}
    ${BSP_SOURCES}
    ${USB_SOURCES}
)
set_target_properties(stm32_freertos_server PROPERTIES SUFFIX ".elf")

target_include_directories(stm32_freertos_server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc"     # FreeRTOSConfig.h, stm32f4xx_hal_conf.h, usbd_conf.h
    "${SAMPLE_DIR}/common"
    ${DMQ_INCLUDE_DIR}
    "${RTOS_DIR}/include"
    "${RTOS_DIR}/portable"
    "${STM32_REPO}/Drivers/CMSIS/Include"
    "${STM32_REPO}/Drivers/CMSIS/Device/ST/STM32F4xx/Include"
    "${HAL_DIR}/Inc"
    "${BSP_DIR}/STM32F4-Discovery"
    "${USB_DIR}/Core/Inc"
    "${USB_DIR}/Class/HID/Inc"
)

target_link_options(stm32_freertos_server PRIVATE
    ${MCU_FLAGS}
    "-T${IDE_DIR}/STM32F407VGTX_FLASH.ld"
    --specs=nano.specs
    -Wl,--gc-sections
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/stm32_freertos_server.map"
    -Wl,--print-memory-usage
)
target_link_libraries(stm32_freertos_server PRIVATE m)

add_custom_command(TARGET stm32_freertos_server POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:stm32_freertos_server> ${CMAKE_CURRENT_BINARY_DIR}/stm32_freertos_server.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:stm32_freertos_server> ${CMAKE_CURRENT_BINARY_DIR}/stm32_freertos_server.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:stm32_freertos_server>
    COMMENT "Creating stm32_freertos_server.hex / .bin"
)
