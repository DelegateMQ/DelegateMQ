# DelegateMQ on ESP32 (ESP-IDF, QEMU)

DelegateMQ's FreeRTOS port on an ESP32 (dual-core Xtensa LX6) under ESP-IDF, run under [Espressif's QEMU](https://github.com/espressif/esp-toolchain-docs/tree/main/qemu/esp32). No board needed.

What the other FreeRTOS samples ([freertos](../freertos/README.md), [freertos-bare-metal](../freertos-bare-metal/README.md)) can't show:

- **SMP.** ESP-IDF's FreeRTOS runs tasks on both cores at once. `dmq::os::FreeRTOSCriticalSection` takes its `ESP_PLATFORM` branch there: a `portMUX_TYPE` spinlock, which also keeps out the other core.
- **Xtensa.** A third CPU architecture for the FreeRTOS port, after Arm and x86 (and RISC-V for bare metal).
- **ESP-IDF.** Its packaging of FreeRTOS (headers under `freertos/`, see `FreeRTOSHeaders.h`), its C++ library and its build.

## The Tests

1. Unicast and multicast delegates
2. Async and async-wait delegates on a worker thread (the async-wait call returns the worker's result)
3. `Signal` with a `ScopedConnection`
4. The portable thread tests ([test/portable](../../../../test/portable/PortableTests.h)): cross-thread dispatch, `FullPolicy`, `StartGate`
5. The portable timer tests: `PacedDispatch`, `TimerDelegate`, `Timer::ProcessTimers()`
6. `dmq::util::Timer` started and stopped **from a hardware interrupt** (an `esp_timer` at 2 kHz with `ESP_TIMER_ISR` dispatch, so the callback runs in the timer interrupt), while a task on each core and the test task do the same. Every entry must be seen as an interrupt (`xPortInIsrContext()`), and the one-shot started from the interrupt must fire.
7. `dmq::util::IsrDelegate` posted from the same kind of interrupt, its target async to a worker thread, through a drain task the post handler wakes. The worker uses `FullPolicy::DROP`: every post must be called once and in order, or counted as dropped (ring or worker queue full).
8. `SetCoreAffinity()`: a `dmq::os::Thread` pinned to core 0, then one pinned to core 1 (`xTaskCreatePinnedToCore()`), each asked 200 times which core runs it (`xPortGetCoreID()`): never the other one. A mask with all cores means no affinity.

`Timer::ProcessTimers()` runs every 10 ms in the FreeRTOS timer service task. The program prints `ALL TESTS PASSED`. A failed check faults (`DMQ_ASSERT_TRUE` → `FaultHandler` → `abort()`) and the panic handler halts (`CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT`).

## Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/linux-macos-setup.html) v5.4, installed for the ESP32 (`./install.sh esp32`)
- Espressif's QEMU: `python $IDF_PATH/tools/idf_tools.py install qemu-xtensa`. On Linux it needs `libgcrypt20`, `libglib2.0-0`, `libpixman-1-0`, `libsdl2-2.0-0` and `libslirp0`.

`docker/Dockerfile` installs both, and `build_all.py --full` builds and runs this sample on Linux (see "Full Test Suite" in [docs/BUILD.md](../../../../docs/BUILD.md)).

## Build and Run

```bash
. $IDF_PATH/export.sh
idf.py build
idf.py qemu monitor        # Ctrl+] to quit
```

Or by hand, as `build_all.py` does:

```bash
cmake -G Ninja -B build -DSDKCONFIG=build/sdkconfig .
cmake --build build
cd build
python -m esptool --chip esp32 merge_bin --fill-flash-size 4MB -o flash.bin @flash_args
qemu-system-xtensa -nographic -machine esp32 -m 4M -nic none \
    -global driver=timer.esp32.timg,property=wdt_disable,value=true \
    -drive file=flash.bin,if=mtd,format=raw
```

QEMU keeps running after the tests (`Ctrl+A X` quits). On a real ESP32 board, `idf.py -p <port> flash monitor` runs the same image.

## Files

- `main/main.cpp`: the tests and the ESP-IDF entry point (`app_main`)
- `main/CMakeLists.txt`: the app component: `main.cpp`, DelegateMQ through `src/DelegateMQ.cpp` (the one file that compiles what the `DMQ_*` options select), the portable tests, and the options (`DMQ_THREAD_FREERTOS`)
- `sdkconfig.defaults`: RTTI on (optional: DelegateMQ also builds without it; C++ exceptions stay off, so DelegateMQ faults instead of throwing), the timer service task's priority and stack, ISR dispatch for `esp_timer`, halt on panic
- `CMakeLists.txt`: the ESP-IDF project

## Notes

- The interrupt is an `esp_timer` alarm rather than a GPTimer: Espressif's QEMU fires a GPTimer alarm once and never again, re-armed or not.
- The test task (priority 5) isn't pinned, so ESP-IDF runs it on either core. The portable tests' consumer threads run at priority 1, below it (see `PortableTests::Options`).
- `build_all.py` runs QEMU without a watchdog (`wdt_disable`) and stops it at `ALL TESTS PASSED` or a fault (`full_suite.ESP32_*`).
