#ifndef SENSOR_NODE_ESP32_HAL_H
#define SENSOR_NODE_ESP32_HAL_H

/// @file Esp32Hal.h
/// @brief The sensor node's hardware on the ESP32-S3-DevKitC-1: the chip's
/// temperature sensor, the RGB LED, and the BOOT button as a touch pad. The
/// fast signal is still the generated test sine (TestSignal.h).

#include "Hal.h"

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

namespace sensornode::esp32s3 {

/// The ESP32-S3's internal temperature sensor: the die temperature, a few
/// degrees above the room's. Read on ControlThread only.
class ChipTemperature : public ITemperatureSensor {
public:
    ChipTemperature() {
        // Not TEMPERATURE_SENSOR_CONFIG_DEFAULT(): it leaves .flags out (a warning)
        temperature_sensor_config_t config = {};
        config.range_min = 10;
        config.range_max = 50;
        config.clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT;
        DMQ_ASSERT_TRUE(temperature_sensor_install(&config, &m_sensor) == ESP_OK);
        DMQ_ASSERT_TRUE(temperature_sensor_enable(m_sensor) == ESP_OK);
    }

    float ReadCelsius() override {
        float celsius = 0.0f;
        temperature_sensor_get_celsius(m_sensor, &celsius);
        return celsius;
    }

private:
    temperature_sensor_handle_t m_sensor = nullptr;
};

/// The board's WS2812 RGB LED. Set() runs on ControlThread only.
class RgbLed : public ILed {
public:
    RgbLed() {
        led_strip_config_t strip = {};
        strip.strip_gpio_num = CONFIG_APP_LED_GPIO;
        strip.max_leds = 1;
        strip.led_model = LED_MODEL_WS2812;
        led_strip_rmt_config_t rmt = {};
        rmt.resolution_hz = 10 * 1000 * 1000;   // 10 MHz
        DMQ_ASSERT_TRUE(led_strip_new_rmt_device(&strip, &rmt, &m_led) == ESP_OK);
        led_strip_clear(m_led);
    }

    void Set(uint8_t r, uint8_t g, uint8_t b) override {
        led_strip_set_pixel(m_led, 0, r, g, b);
        led_strip_refresh(m_led);
    }

private:
    led_strip_handle_t m_led = nullptr;
};

/// The BOOT button as touch pad 0: its GPIO interrupt posts each press and
/// release to the IsrDelegate (copied into its ring; no heap, no mutex).
/// Bounces inside 30 ms are ignored.
class ButtonTouch : public ITouchInput {
public:
    void Start(Sink& sink) override {
        m_sink = &sink;
        m_lastLevel = 1;
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << CONFIG_APP_BUTTON_GPIO;
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.intr_type = GPIO_INTR_ANYEDGE;
        DMQ_ASSERT_TRUE(gpio_config(&io) == ESP_OK);
        const esp_err_t err = gpio_install_isr_service(0);
        DMQ_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);    // already installed
        DMQ_ASSERT_TRUE(gpio_isr_handler_add(Pin(), &ButtonTouch::Isr, this) == ESP_OK);
    }

    void Stop() override {
        gpio_isr_handler_remove(Pin());
    }

private:
    static constexpr TickType_t DEBOUNCE_TICKS = pdMS_TO_TICKS(30);

    static gpio_num_t Pin() { return static_cast<gpio_num_t>(CONFIG_APP_BUTTON_GPIO); }

    // GPIO interrupt (the core that called gpio_install_isr_service())
    static void Isr(void* arg) {
        auto* self = static_cast<ButtonTouch*>(arg);
        const int level = gpio_get_level(Pin());
        const TickType_t now = xTaskGetTickCountFromISR();
        if (level == self->m_lastLevel || now - self->m_lastEdge < DEBOUNCE_TICKS)
            return;
        self->m_lastLevel = level;
        self->m_lastEdge = now;
        self->m_sink->Post(0, level == 0 ? 1 : 0);      // pressed (low) = touched
    }

    Sink* m_sink = nullptr;
    int m_lastLevel = 1;            // interrupt only, after Start()
    TickType_t m_lastEdge = 0;
};

/// esp_timer: microseconds since boot, 64 bits
class EspFineClock : public IFineClock {
public:
    uint64_t NowNs() override { return static_cast<uint64_t>(esp_timer_get_time()) * 1000u; }
};

} // namespace sensornode::esp32s3

#endif // SENSOR_NODE_ESP32_HAL_H
