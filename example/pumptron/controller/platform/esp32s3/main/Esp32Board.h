#ifndef PUMPTRON_ESP32_BOARD_H
#define PUMPTRON_ESP32_BOARD_H

/// @file Esp32Board.h
/// @brief IBoard backed by the ESP32-S3-DevKitC-1's own peripherals.
///
/// - Ambient temperature: the chip's internal temperature sensor (the die, a
///   few degrees above the room, like the F4's), low-pass filtered.
/// - Vibration: none; the board has no accelerometer, so only the pump model's
///   vibration shows. (An I2C accelerometer would plug in here.)
/// - Local stop: the BOOT button (GPIO 0, low when pressed), polled.
/// - Indicators: the board has one RGB LED (WS2812) where the F4 has four, so
///   the states become colours: red = fault, green = running, amber = powered
///   and idle. POWER off (it blinks while the link is degraded) turns the LED
///   off, so the blink shows in every state but fault. ACTIVITY isn't shown.
///
/// All methods run on the pump controller thread (IBoard's contract), so the
/// drivers need no locking.

#include "board/IBoard.h"

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "led_strip.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace pumptron {
namespace board {

class Esp32Board : public IBoard {
public:
    void Init() override
    {
        // Not TEMPERATURE_SENSOR_CONFIG_DEFAULT(): it leaves .flags out (a warning)
        temperature_sensor_config_t temp = {};
        temp.range_min = 10;
        temp.range_max = 50;
        temp.clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT;
        m_tempOk = temperature_sensor_install(&temp, &m_temp) == ESP_OK &&
                   temperature_sensor_enable(m_temp) == ESP_OK;
        if (!m_tempOk)
            printf("Esp32Board: WARNING - temperature sensor init failed, ambient fixed at 25 C\n");

        led_strip_config_t strip = {};
        strip.strip_gpio_num = CONFIG_APP_LED_GPIO;
        strip.max_leds = 1;
        strip.led_model = LED_MODEL_WS2812;
        led_strip_rmt_config_t rmt = {};
        rmt.resolution_hz = 10 * 1000 * 1000;   // 10 MHz
        m_ledOk = led_strip_new_rmt_device(&strip, &rmt, &m_led) == ESP_OK;
        if (m_ledOk)
            led_strip_clear(m_led);
        else
            printf("Esp32Board: WARNING - RGB LED init failed\n");

        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << CONFIG_APP_BUTTON_GPIO;
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io);
    }

    void Sample(float dtSec) override
    {
        float c = 0.0f;
        if (m_tempOk && temperature_sensor_get_celsius(m_temp, &c) == ESP_OK) {
            if (!m_ambientSeeded) {
                m_ambientC = c;
                m_ambientSeeded = true;
            } else if (dtSec > 0.0f) {
                m_ambientC += (1.0f - std::exp(-dtSec / TEMP_TAU_S)) * (c - m_ambientC);
            }
        }
    }

    float ReadAmbientTempC() override { return m_ambientC; }
    float ReadVibrationG() override { return 0.0f; }

    bool IsLocalStopPressed() override
    {
        return gpio_get_level(static_cast<gpio_num_t>(CONFIG_APP_BUTTON_GPIO)) == 0;
    }

    void SetIndicator(Indicator indicator, bool on) override
    {
        m_state[static_cast<int>(indicator)] = on;
        UpdateLed();
    }

    void ToggleIndicator(Indicator indicator) override
    {
        const int i = static_cast<int>(indicator);
        m_state[i] = !m_state[i];
        UpdateLed();
    }

    const char* Name() const override { return "ESP32-S3-DevKitC-1"; }

private:
    static constexpr float TEMP_TAU_S = 1.0f;       // die temperature smoothing
    static constexpr uint8_t LEVEL = 32;            // of 255: a WS2812 at full is glaring

    bool On(Indicator indicator) const { return m_state[static_cast<int>(indicator)]; }

    /// One LED for four indicators, highest priority first
    void UpdateLed()
    {
        uint32_t rgb = 0;                                       // off
        if (On(Indicator::FAULT))
            rgb = Rgb(LEVEL, 0, 0);                             // red
        else if (!On(Indicator::POWER))
            rgb = 0;                                            // off, or the link-degraded blink
        else if (On(Indicator::RUNNING))
            rgb = Rgb(0, LEVEL, 0);                             // green
        else
            rgb = Rgb(LEVEL, LEVEL / 3, 0);                     // amber

        if (!m_ledOk || rgb == m_rgb)
            return;     // the RMT refresh only when the colour changes
        m_rgb = rgb;
        led_strip_set_pixel(m_led, 0, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        led_strip_refresh(m_led);
    }

    static uint32_t Rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }

    temperature_sensor_handle_t m_temp = nullptr;
    led_strip_handle_t m_led = nullptr;
    bool m_tempOk = false;
    bool m_ledOk = false;
    bool m_ambientSeeded = false;
    float m_ambientC = 25.0f;
    bool m_state[4] = {};
    uint32_t m_rgb = 0;
};

} // namespace board
} // namespace pumptron

#endif // PUMPTRON_ESP32_BOARD_H
