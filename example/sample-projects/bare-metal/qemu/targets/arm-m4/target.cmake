# Cortex-M4 on QEMU mps2-an386: own vector table and reset handler
# (startup.c), semihosting syscalls (retarget.c), flash.ld memory layout,
# SysTick (bsp.cpp).

target_sources(${DELEGATE_APP} PRIVATE
    ${BM_TARGET_DIR}/startup.c
    ${BM_TARGET_DIR}/../common/retarget.c
    ${BM_TARGET_DIR}/bsp.cpp
)

target_link_options(${DELEGATE_APP} PRIVATE
    "-T${BM_TARGET_DIR}/flash.ld"
    "-Wl,-Map=${CMAKE_BINARY_DIR}/output.map"
)
target_link_libraries(${DELEGATE_APP} PRIVATE c gcc nosys)
