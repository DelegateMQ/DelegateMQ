# Sensor Node on the ESP32-S3 (in progress)

The ESP-IDF build of the sensor node for the ESP32-S3-DevKitC-1 N8R8: the same `node/app` code as the desktop build, with the board's peripherals behind `node/app/Hal.h`.

Planned:

1. **Core pinning:** done in the library (`Thread::SetCoreAffinity()`, `xTaskCreatePinnedToCore()` on ESP-IDF, checked on both ESP32 cores by `os/freertos-esp32` Test 8). The board build sets `NodeConfig::appCoreMask = 1 << 1` and `netCoreMask = 1 << 0`; the node measures its cross-core round trip at startup.
2. **Network:** Wi-Fi in station mode (SSID and password from `menuconfig`), and a UDP transport for ESP-IDF's lwIP sockets.
3. **Peripherals:** the ADC (continuous mode, DMA), the internal temperature sensor, the capacitive touch pads (interrupt to `IsrDelegate`), the WS2812 RGB LED.
4. **Platform task:** `Timer::ProcessTimers()`, `IsrDelegates::ProcessAll()` and `Thread::WatchdogCheckAll()`, as `node/platform/desktop/main.cpp` does in its loop.

Built with PlatformIO (`framework = espidf`, `espressif32@6.11.0`, ESP-IDF 5.4.1) or `idf.py`, like [boards/esp32s3-devkitc](../../../../sample-projects/boards/esp32s3-devkitc/README.md).
