/// @file main.cpp
/// @brief The sensor node on the ESP32-S3-DevKitC-1 (N8R8): the board's
/// hardware (Esp32Hal.h) and Wi-Fi, then the same program as the desktop
/// build, RunNode().
///
/// SampleThread and ControlThread run on core 1; NetThread on core 0 with
/// ESP-IDF's Wi-Fi and TCP/IP tasks. The platform loop runs in this task.

#include "NodeMain.h"
#include "Esp32Hal.h"
#include "TestSignal.h"
#include "Wifi.h"

#include "sdkconfig.h"

#include <cstdio>

using namespace sensornode;

extern "C" void app_main()
{
    printf("\nSensor node on %s\n", CONFIG_IDF_TARGET);
    if (!esp32s3::StartWifi(30000)) {
        printf("Wi-Fi: not connected. Check the SSID and password in menuconfig.\n");
        return;
    }
    printf("On the PC: sensor_pc 0 %s\n", esp32s3::WifiAddress());

    SineSource signal;
    esp32s3::ChipTemperature temperature;
    esp32s3::RgbLed led;
    esp32s3::ButtonTouch touch;
    Platform platform{ signal, temperature, led, touch };

    NodeConfig config;
    config.pcAddress = CONFIG_APP_PC_ADDRESS;
    config.appCoreMask = 1u << 1;       // sampling and control
    config.netCoreMask = 1u << 0;       // the network, with ESP-IDF's Wi-Fi tasks
    RunNode(platform, config);          // runs until reset
}
