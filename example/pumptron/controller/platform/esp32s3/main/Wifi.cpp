#include "Wifi.h"

#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <cstdio>
#include <cstring>

namespace pumptron::esp32s3 {

namespace {
    constexpr EventBits_t GOT_IP = BIT0;
    EventGroupHandle_t s_events = nullptr;
    char s_address[16] = "0.0.0.0";

    // ESP-IDF's event loop task
    void OnEvent(void*, esp_event_base_t base, int32_t id, void* data)
    {
        if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
            xEventGroupClearBits(s_events, GOT_IP);
            printf("Wi-Fi: disconnected, reconnecting\n");
            esp_wifi_connect();
        } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            const auto* event = static_cast<const ip_event_got_ip_t*>(data);
            snprintf(s_address, sizeof(s_address), IPSTR, IP2STR(&event->ip_info.ip));
            printf("Wi-Fi: connected, address %s\n", s_address);
            xEventGroupSetBits(s_events, GOT_IP);
        }
    }
}

bool StartWifi(uint32_t timeoutMs)
{
    if (strlen(CONFIG_APP_WIFI_SSID) == 0) {
        printf("Wi-Fi: no SSID set. Run 'pio run -t menuconfig' (or 'idf.py menuconfig'), "
               "\"Pumptron\" menu.\n");
        return false;
    }

    esp_err_t err = nvs_flash_init();       // Wi-Fi keeps calibration data in NVS
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &OnEvent, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &OnEvent, nullptr, nullptr));

    wifi_config_t config = {};
    strncpy(reinterpret_cast<char*>(config.sta.ssid), CONFIG_APP_WIFI_SSID, sizeof(config.sta.ssid));
    strncpy(reinterpret_cast<char*>(config.sta.password), CONFIG_APP_WIFI_PASSWORD, sizeof(config.sta.password));
    config.sta.threshold.authmode = strlen(CONFIG_APP_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // No modem sleep: with it the radio wakes only every DTIM beacon (about
    // 100-300 ms), which would delay the PC's commands and ACKs
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    printf("Wi-Fi: joining \"%s\"\n", CONFIG_APP_WIFI_SSID);
    const EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeoutMs));
    return (bits & GOT_IP) != 0;
}

const char* WifiAddress()
{
    return s_address;
}

} // namespace pumptron::esp32s3
