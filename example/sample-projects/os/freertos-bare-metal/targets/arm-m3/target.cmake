# Cortex-M3 on QEMU mps2-an385: own vector table, reset handler and FreeRTOS
# hooks (startup.c, main.c), the CMSDK timer interrupt (bsp.c), fault
# handlers that end the run (fault.cpp). The newlib syscalls over semihosting
# and the linker script are the bare-metal/qemu sample's (same MPS2 memory map:
# 4 MB flash at 0, 4 MB RAM at 0x20000000).

set(BARE_METAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../bare-metal/qemu/targets")

target_sources(${DELEGATE_APP} PRIVATE
    ${BM_TARGET_DIR}/startup.c
    ${BM_TARGET_DIR}/main.c
    ${BM_TARGET_DIR}/bsp.c
    ${BM_TARGET_DIR}/fault.cpp
    ${BARE_METAL_DIR}/common/retarget.c
)
target_compile_definitions(${DELEGATE_APP} PRIVATE BSP_TARGET DMQ_CUSTOM_FAULT_HANDLER)

target_link_options(${DELEGATE_APP} PRIVATE
    "-T${BARE_METAL_DIR}/arm-m4/flash.ld"
    "-Wl,-Map=${CMAKE_BINARY_DIR}/output.map"
)
target_link_libraries(${DELEGATE_APP} PRIVATE c gcc nosys)
