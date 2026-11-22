# Cortex-M33 on QEMU mps2-an505: arm-m4's vector table and reset handler
# (../arm-m4/startup.c: the Armv8-M core exceptions, FPU enable and CPACR
# address are the same), semihosting syscalls (retarget.c), an505.ld memory
# layout, SysTick (bsp.cpp).

target_sources(${DELEGATE_APP} PRIVATE
    ${BM_TARGET_DIR}/../arm-m4/startup.c
    ${BM_TARGET_DIR}/../common/retarget.c
    ${BM_TARGET_DIR}/bsp.cpp
)

target_link_options(${DELEGATE_APP} PRIVATE
    "-T${BM_TARGET_DIR}/an505.ld"
    "-Wl,-Map=${CMAKE_BINARY_DIR}/output.map"
)
target_link_libraries(${DELEGATE_APP} PRIVATE c gcc nosys)
