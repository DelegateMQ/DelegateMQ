# Cortex-M0 on QEMU microbit (nRF51822: 256 KB flash, 16 KB RAM): own vector
# table and reset handler (startup.c), semihosting syscalls
# (../common/retarget.c, shared with arm-m4), nrf51.ld memory layout, TIMER0
# interrupt (bsp.cpp).

target_sources(${DELEGATE_APP} PRIVATE
    ${BM_TARGET_DIR}/startup.c
    ${BM_TARGET_DIR}/../common/retarget.c
    ${BM_TARGET_DIR}/bsp.cpp
)

target_link_options(${DELEGATE_APP} PRIVATE
    "-T${BM_TARGET_DIR}/nrf51.ld"
    "-Wl,-Map=${CMAKE_BINARY_DIR}/output.map"
    "-Wl,--print-memory-usage"
)
target_link_libraries(${DELEGATE_APP} PRIVATE c gcc nosys)
