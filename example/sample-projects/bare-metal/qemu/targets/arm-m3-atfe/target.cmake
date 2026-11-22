# Cortex-M3 on QEMU mps2-an385, everything from ATfE's runtime: no startup.c
# or retarget.c here.
#   crt0-semihost.o  vector table, reset handler, .data/.bss and C++ static
#                    constructor init
#   libsemihost.a    printf, exit, ... through semihosting
#   picolibcpp.ld    linker script, laid out by the --defsym values below

set(ATFE_RUNTIME_DIR "${ATFE_PATH}/lib/clang-runtimes/arm-none-eabi/armv7m_soft_nofp_exn_rtti_size")

target_sources(${DELEGATE_APP} PRIVATE ${BM_TARGET_DIR}/bsp.cpp)

# Memory layout for QEMU mps2-an385 (Cortex-M3):
#   boot_flash  4 KB    @ 0x00000000  (vector table + _start)
#   flash       4092 KB @ 0x00001000  (code + rodata)
#   ram         4 MB    @ 0x20000000
target_link_options(${DELEGATE_APP} PRIVATE
    "--target=thumbv7m-unknown-none-eabi"
    "-fuse-ld=${ATFE_PATH}/bin/ld.lld${ATFE_EXE}"
    "-nostartfiles"       # no automatic crt0.o; crt0-semihost.o is linked explicitly
    "-Wl,--defsym=__boot_flash=0x00000000"
    "-Wl,--defsym=__boot_flash_size=0x00001000"
    "-Wl,--defsym=__flash=0x00001000"
    "-Wl,--defsym=__flash_size=0x003FF000"
    "-Wl,--defsym=__ram=0x20000000"
    "-Wl,--defsym=__ram_size=0x00400000"
    "-T${ATFE_RUNTIME_DIR}/lib/picolibcpp.ld"
    "-Wl,-Map=${CMAKE_BINARY_DIR}/output.map"
    "${ATFE_RUNTIME_DIR}/lib/crt0-semihost.o"
    "-L${ATFE_RUNTIME_DIR}/lib"
    "-lc++"
    "-lc++abi"
    "-lunwind"
    "-lsemihost"
    "-lc"
    "-lm"
    "-lclang_rt.builtins"
)
