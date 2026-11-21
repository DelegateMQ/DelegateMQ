# ESP32-S3-DevKitC-1 (ESP-IDF, FreeRTOS SMP)

DelegateMQ on an Espressif **ESP32-S3-DevKitC-1 N8R8**: a dual-core Xtensa LX7 with 8 MB flash and 8 MB octal PSRAM, Wi-Fi and Bluetooth LE, running ESP-IDF's FreeRTOS on both cores. The hardware-free counterpart, on a plain ESP32 under QEMU, is [os/freertos-esp32](../../os/freertos-esp32/README.md).

A simple board demo: DelegateMQ's synchronous and asynchronous calls on the board's RGB LED and BOOT button. No network, and no thread is pinned to a core (ESP-IDF schedules them on either). The multi-core, networked app is [example/sensor-node](../../../sensor-node/README.md).

An `App` object owns a worker thread (`AppThread`) and the LED; only `AppThread` touches the LED, so there's no lock. A second thread, `LogThread`, keeps statistics. At startup the app shows each kind of call once:

1. **Synchronous delegate:** `Add(2, 3)` runs on the caller's task.
2. **Async delegate:** `SetColour(blue)` is queued to `AppThread`; the caller doesn't wait.
3. **Async-wait delegate:** `GetColourName()` runs on `AppThread` and the caller waits for the result. It runs after the queued `SetColour()`, so it returns "blue".
4. **Signal:** `App::OnColourChanged` has two subscribers: a function called on `AppThread`, inside the signal, and a `Stats` object called on `LogThread`.

Then it runs on its own:

5. **Timer:** a `dmq::util::Timer` blinks the LED every 500 ms. Its `OnExpired` is a `MakeTimerDelegate` to `AppThread`: each tick runs there, at most one in flight.
6. **Interrupt:** the **BOOT** button's GPIO interrupt posts the tick count to a `dmq::util::IsrDelegate` (no heap, no mutex in the interrupt). A drain task, woken by the post handler, runs `IsrDelegates::ProcessAll()`, which sends the press to `AppThread`. Each press changes the colour and fires `OnColourChanged`.

`Timer::ProcessTimers()` runs every 10 ms in the FreeRTOS timer service task. Each line on the serial console names the task that printed it, so you can see where every call ran. At startup it also prints the chip, the number of cores and the PSRAM size.

## The Board

| Item | Detail |
| :--- | :--- |
| USB-C "UART" port | A USB-to-UART bridge: flashing and the serial console (115200) |
| USB-C "USB" port | The ESP32-S3's own USB: flashing, and built-in USB-JTAG for debugging (`debug_tool = esp-builtin`), no probe needed. It re-enumerates on every reset, so the console is easier to follow on the "UART" port |
| RGB LED | One WS2812 on **GPIO 38** (board v1.1, the default) or **GPIO 48** (v1.0); set `CONFIG_APP_LED_GPIO` in menuconfig |
| BOOT button | GPIO 0, low when pressed (`CONFIG_APP_BUTTON_GPIO`) |
| Flash | 8 MB, quad I/O |
| PSRAM | 8 MB, octal (`CONFIG_SPIRAM_MODE_OCT`); it uses GPIO 35-37 |

## ESP-IDF Version

ESP-IDF **5.4.1**, the version `docker/Dockerfile` uses for `os/freertos-esp32`. In PlatformIO that's `platform = espressif32@6.11.0`; newer platform releases bundle newer ESP-IDF versions. Change both together.

## Build and Flash with PlatformIO

```bash
cd example/sample-projects/boards/esp32s3-devkitc
pio run                          # build (the first build downloads ESP-IDF and the toolchain)
pio run -t upload                # flash through the "UART" port
pio device monitor               # serial console; Ctrl+C to quit
pio run -t menuconfig            # ESP-IDF configuration (LED GPIO, ...)
```

Both USB ports can flash. If an upload through the "UART" port fails with "Invalid head of packet" (seen with a CH343 bridge at 921600 baud), flash through the "USB" port instead, `pio run -t upload --upload-port <its COM port>`, and keep the console on the "UART" port. With both ports connected, pass `--upload-port` anyway, since PlatformIO picks one on its own.

Changing a menuconfig default (`main/Kconfig.projbuild`) doesn't change an existing `sdkconfig.esp32s3`: delete it, or set the value with `pio run -t menuconfig`.

The project is a plain ESP-IDF project (`CMakeLists.txt`, `main/`); `platformio.ini` points PlatformIO at it (`src_dir = main`). PlatformIO keeps its configuration in `sdkconfig.esp32s3`, created from `sdkconfig.defaults`.

## Build and Flash with ESP-IDF

```bash
. $IDF_PATH/export.sh            # ESP-IDF 5.4.1
cd example/sample-projects/boards/esp32s3-devkitc
idf.py set-target esp32s3        # once
idf.py build flash monitor
```

The LED driver (`espressif/led_strip`) comes from the ESP-IDF component manager (`main/idf_component.yml`) on the first build. `build_all.py` doesn't build this sample: it needs the board.
