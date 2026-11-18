# RV32IMC on QEMU virt (-bios none: direct machine-mode boot, no OpenSBI):
# own _start (startup.S; the toolchain's crt0.o can't be used, see there),
# machine-mode trap vector (trap_entry.S), virt.ld memory layout, CLINT timer
# and trap dispatch (bsp.cpp). libsemihost.a (semihost.specs in
# toolchain.cmake) provides printf, _sbrk and _exit through semihosting.

target_sources(${DELEGATE_APP} PRIVATE
    ${BM_TARGET_DIR}/startup.S
    ${BM_TARGET_DIR}/trap_entry.S
    ${BM_TARGET_DIR}/bsp.cpp
)

# -march/-mabi/--specs are already in the compiler flags, which CMake also
# passes to the link; repeating --specs=semihost.specs here would apply its
# "%rename lib" directive twice, which GCC rejects.
target_link_options(${DELEGATE_APP} PRIVATE
    "-nostartfiles" # startup.S provides _start, not crt0.o
    "-T${BM_TARGET_DIR}/virt.ld"
    "-Wl,-Map=${CMAKE_BINARY_DIR}/output.map"
)
