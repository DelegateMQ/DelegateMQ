# Sensor Node on the ESP32-S3

The ESP-IDF build of the [sensor node](../../../README.md) for the Espressif **ESP32-S3-DevKitC-1 N8R8**: the same `node/app` code as the desktop build, with the board's hardware behind `node/app/Hal.h`, and Wi-Fi to the PC client.

| File | Contents |
| :--- | :--- |
| `main/main.cpp` | Wi-Fi, the hardware objects, `RunNode()` |
| `main/Esp32Hal.h` | The chip's temperature sensor, the RGB LED, the BOOT button as touch pad 0 (GPIO interrupt to the node's `IsrDelegate`) |
| `main/Wifi.cpp` | Station mode, waits for an address, reconnects on its own |
| `main/Kconfig.projbuild` | menuconfig: Wi-Fi SSID and password, the PC's address, the LED and button GPIOs |
| `main/CMakeLists.txt` | The component: `node/app`, this folder, DelegateMQ (`src/DelegateMQ.cpp`) |

## On the Board

- **Cores:** `SampleThread` and `ControlThread` pinned to core 1, `NetThread` to core 0 with ESP-IDF's Wi-Fi and TCP/IP tasks (`Thread::SetCoreAffinity()`, `xTaskCreatePinnedToCore()`).
- **Transport:** DelegateMQ's lwIP UDP transport (`DMQ_TRANSPORT_ARM_LWIP_UDP`, `port/transport/arm-lwip-udp`): ESP-IDF's network stack is lwIP, so its BSD socket API is there. Despite the folder's name nothing in it is ARM specific.
- **Wi-Fi power save off** (`WIFI_PS_NONE`): with modem sleep the radio wakes only every DTIM beacon, which would delay the PC's commands and ACKs by 100-300 ms.
- **Stacks:** `DMQ_FREERTOS_STACK_WORDS=8192`. On ESP-IDF a stack is sized in bytes (`StackType_t` is one byte), and `NetThread` serializes and sends from its stack.
- **Flash:** about 1.07 MB with DataBus, the serializer and Wi-Fi, so the 1.5 MB app partition (`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`; `board_build.partitions` tells PlatformIO) and `-Os`. Static RAM: about 42 KB.
- **Fast signal:** the generated test sine (`node/app/TestSignal.h`), so the PC client's self-test checks the board too. The ADC is next.

## Configure

The Wi-Fi credentials and the PC's address are set in menuconfig ("Sensor node" menu), never in a committed file:

```bash
cd example/sensor-node/node/platform/esp32s3
pio run -t menuconfig
```

They land in `sdkconfig.esp32s3` (PlatformIO) or `sdkconfig` (`idf.py`), both ignored by git. The firmware image and the board's NVS also hold the password, in plain text (no flash encryption); `esptool.py erase_flash` clears the board.

The PC's address is where the node publishes; on Windows, `ipconfig` shows it. The board needs a 2.4 GHz network.

## Build and Flash

With PlatformIO (`framework = espidf`, `espressif32@6.11.0`, ESP-IDF 5.4.1, like [boards/esp32s3-devkitc](../../../../sample-projects/boards/esp32s3-devkitc/README.md)):

```bash
pio run                          # build
pio run -t upload --upload-port COMx
pio device monitor --port COMy   # 115200
```

The board's two USB ports both flash; the console is easier to follow on the "UART" port, since the "USB" port re-enumerates on every reset. With `idf.py`: `idf.py set-target esp32s3`, `idf.py menuconfig`, `idf.py build flash monitor`.

The node prints its address once Wi-Fi is up:

```
Wi-Fi: connected, address 192.168.0.241
On the PC: sensor_pc 0 192.168.0.241
SensorNode: waiting for a PC client (it sends a start every second)
```

## Run the PC Client

Build `sensor_pc` on the PC ([README](../../../README.md#build-and-run-on-the-desktop)) and give it the board's address:

```bash
sensor_pc 5 192.168.0.241        # self-test, then exit
sensor_pc 0 192.168.0.241        # self-test, then show the node's data
```

Windows Firewall asks the first time whether `sensor_pc` may receive on the network: allow it on private networks, or nothing from the board arrives. The touch test (test 5) waits 30 s for you to press and release BOOT.

## Measured on the Board

| | |
| :--- | :--- |
| Async-wait round trip, `ControlThread` (core 1) to `NetThread` (core 0) | about 207 µs mean |
| Async-wait round trip, `ControlThread` to `SampleThread` (both core 1) | about 233 µs mean |

Both are far above the desktop's (1-20 µs) and about equal, so the cost is in the call itself, not in crossing cores. Under investigation.
