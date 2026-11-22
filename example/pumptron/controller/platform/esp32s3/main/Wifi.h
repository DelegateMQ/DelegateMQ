#ifndef PUMPTRON_WIFI_H
#define PUMPTRON_WIFI_H

/// @file Wifi.h
/// @brief Wi-Fi station bring-up for the ESP32-S3 controller (ESP-IDF).

#include <cstdint>

namespace pumptron::esp32s3 {

/// Join the network from menuconfig (CONFIG_APP_WIFI_SSID/PASSWORD) and wait
/// for an IPv4 address. Reconnects on its own after a disconnect.
/// @return false if no SSID is set or no address came within timeoutMs.
bool StartWifi(uint32_t timeoutMs);

/// The station's IPv4 address as text ("0.0.0.0" before StartWifi() succeeds)
const char* WifiAddress();

} // namespace pumptron::esp32s3

#endif // PUMPTRON_WIFI_H
