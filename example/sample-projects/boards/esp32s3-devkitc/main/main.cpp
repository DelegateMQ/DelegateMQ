/// @file main.cpp
/// @brief DelegateMQ on an ESP32-S3-DevKitC-1 (N8R8): sync and async delegates
/// on the board's RGB LED and BOOT button.
///
/// An App object owns a worker thread (AppThread) and the LED; only AppThread
/// touches the LED, so it needs no lock. A second thread (LogThread) keeps
/// statistics. At startup app_main() shows each kind of call once:
///
///  1. Synchronous delegate: runs on the caller's thread.
///  2. Async delegate: queued to AppThread, the caller doesn't wait.
///  3. Async-wait delegate: runs on AppThread, the caller waits for the result.
///  4. Signal: OnColourChanged has two subscribers, one called on AppThread
///     (synchronous) and one queued to LogThread (async). The name goes as a
///     std::string: an async delegate copies the data behind a pointer
///     argument, and for a const char* that is a single char.
///
/// Then it runs on its own:
///
///  5. Timer: a dmq::util::Timer blinks the LED. Its OnExpired is a
///     MakeTimerDelegate to AppThread (at most one tick in flight).
///  6. Interrupt: the BOOT button's GPIO interrupt posts to an IsrDelegate (no
///     heap, no mutex in the interrupt). A drain task, woken by the post
///     handler, calls IsrDelegates::ProcessAll(), which sends the press to
///     AppThread. Each press changes the LED colour and fires OnColourChanged.
///
/// Timer::ProcessTimers() runs every 10 ms in the FreeRTOS timer service task.
/// See README.md for building, flashing and the board's pins.

#include "DelegateMQ.h"

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_psram.h"
#include "led_strip.h"

#include <cstdio>
#include <string>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;

namespace {

constexpr auto BLINK_PERIOD = std::chrono::milliseconds(500);
constexpr TickType_t DEBOUNCE_TICKS = pdMS_TO_TICKS(200);
constexpr uint8_t LED_LEVEL = 16;       // 0-255 per colour: the LED is bright

struct Colour { const char* name; uint8_t r, g, b; };
constexpr Colour COLOURS[] = {
    { "green", 0, LED_LEVEL, 0 },
    { "blue",  0, 0, LED_LEVEL },
    { "red",   LED_LEVEL, 0, 0 },
    { "white", LED_LEVEL, LED_LEVEL, LED_LEVEL },
};
constexpr int COLOUR_COUNT = sizeof(COLOURS) / sizeof(COLOURS[0]);

/// The name of the FreeRTOS task running the caller
const char* TaskName() { return pcTaskGetName(nullptr); }

// ---------------------------------------------------------------------------
// App: owns AppThread and the LED
// ---------------------------------------------------------------------------
class App
{
public:
    /// Fired on AppThread when the colour changes: (colour index, name).
    /// Not const char*: an async subscriber would get a copy of one char.
    Signal<void(int, const std::string&)> OnColourChanged;

    App() :
        m_thread("AppThread"),
        m_button(MakeDelegate(this, &App::OnButton, m_thread))
    {
    }

    /// Task context, once
    void Start()
    {
        m_thread.CreateThread();

        // The LED is created on AppThread, the only thread that uses it
        MakeDelegate(this, &App::InitLed, m_thread, WAIT_INFINITE)();

        m_blinkConn = m_blinkTimer.OnExpired.Connect(
            MakeTimerDelegate(this, &App::OnBlink, m_thread));
        m_blinkTimer.Start(BLINK_PERIOD);

        InitButton();
    }

    Thread& GetThread() { return m_thread; }

    /// AppThread: set the colour (and announce it)
    void SetColour(int colour)
    {
        m_colour = colour % COLOUR_COUNT;
        printf("[%s] SetColour: %s\n", TaskName(), COLOURS[m_colour].name);
        Show();
        OnColourChanged(m_colour, COLOURS[m_colour].name);
    }

    /// AppThread: the current colour's name
    const char* GetColourName() const { return COLOURS[m_colour].name; }

    /// AppThread: button presses accepted so far
    int GetPressCount() const { return m_presses; }

private:
    void InitLed()
    {
        led_strip_config_t stripConfig = {};
        stripConfig.strip_gpio_num = CONFIG_APP_LED_GPIO;
        stripConfig.max_leds = 1;
        stripConfig.led_model = LED_MODEL_WS2812;

        led_strip_rmt_config_t rmtConfig = {};
        rmtConfig.resolution_hz = 10 * 1000 * 1000;     // 10 MHz

        DMQ_ASSERT_TRUE(led_strip_new_rmt_device(&stripConfig, &rmtConfig, &m_led) == ESP_OK);
        led_strip_clear(m_led);
        printf("LED: WS2812 on GPIO %d\n", CONFIG_APP_LED_GPIO);
    }

    void InitButton()
    {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << CONFIG_APP_BUTTON_GPIO;
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.intr_type = GPIO_INTR_NEGEDGE;              // pressed: high to low
        DMQ_ASSERT_TRUE(gpio_config(&io) == ESP_OK);
        DMQ_ASSERT_TRUE(gpio_install_isr_service(0) == ESP_OK);
        DMQ_ASSERT_TRUE(gpio_isr_handler_add(static_cast<gpio_num_t>(CONFIG_APP_BUTTON_GPIO),
            &App::ButtonIsr, this) == ESP_OK);
        printf("Button: GPIO %d (BOOT)\n", CONFIG_APP_BUTTON_GPIO);
    }

    /// GPIO interrupt: only copies the tick into the IsrDelegate's ring
    static void ButtonIsr(void* arg)
    {
        static_cast<App*>(arg)->m_button.Post(xTaskGetTickCountFromISR());
    }

    /// AppThread: a button press, debounced
    void OnButton(uint32_t tick)
    {
        if (m_presses > 0 && tick - m_lastPress < DEBOUNCE_TICKS)
            return;
        m_presses++;
        m_lastPress = tick;
        printf("[%s] Button press %d\n", TaskName(), m_presses);
        SetColour(m_colour + 1);
    }

    /// AppThread: blink timer tick
    void OnBlink()
    {
        m_ledOn = !m_ledOn;
        Show();
    }

    void Show()
    {
        if (m_ledOn) {
            const Colour& c = COLOURS[m_colour];
            led_strip_set_pixel(m_led, 0, c.r, c.g, c.b);
            led_strip_refresh(m_led);
        } else {
            led_strip_clear(m_led);
        }
    }

    Thread m_thread;
    IsrDelegate<void(uint32_t)> m_button;
    Timer m_blinkTimer;
    ScopedConnection m_blinkConn;

    // AppThread only
    led_strip_handle_t m_led = nullptr;
    bool m_ledOn = true;
    int m_colour = 0;
    int m_presses = 0;
    uint32_t m_lastPress = 0;
};

// ---------------------------------------------------------------------------
// Signal subscribers
// ---------------------------------------------------------------------------

/// Synchronous subscriber: called on AppThread, inside OnColourChanged()
void LogColourChange(int colour, const std::string& name)
{
    printf("[%s] OnColourChanged (sync subscriber): %d %s\n", TaskName(), colour, name.c_str());
}

/// Async subscriber: keeps statistics on LogThread
class Stats
{
public:
    void OnColourChanged(int colour, const std::string& name)
    {
        m_changes++;
        printf("[%s] OnColourChanged (async subscriber): %d %s, %d changes so far\n",
               TaskName(), colour, name.c_str(), m_changes);
    }

private:
    int m_changes = 0;      // LogThread only
};

// ---------------------------------------------------------------------------
// IsrDelegate drain task: woken by the post handler, which runs in the
// interrupt that called Post()
// ---------------------------------------------------------------------------
SemaphoreHandle_t s_drainSem = nullptr;

void WakeDrainTask()
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_drainSem, &woken);
    portYIELD_FROM_ISR(woken);
}

void DrainTask(void*)
{
    for (;;) {
        xSemaphoreTake(s_drainSem, portMAX_DELAY);
        IsrDelegates::ProcessAll();
    }
}

// Timer::ProcessTimers() every 10 ms, from the FreeRTOS timer service task
void ProcessTimersCallback(TimerHandle_t) { Timer::ProcessTimers(); }

int Add(int a, int b) { return a + b; }

void PrintBoard()
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("\nDelegateMQ on %s, %d cores, revision v%d.%d\n", CONFIG_IDF_TARGET, chip.cores,
           chip.revision / 100, chip.revision % 100);
    printf("PSRAM: %u KB\n", static_cast<unsigned>(esp_psram_get_size() / 1024));
}

} // namespace

extern "C" void app_main(void)
{
    PrintBoard();

    s_drainSem = xSemaphoreCreateBinary();
    xTaskCreate(DrainTask, "IsrDrain", 4096, nullptr, 6, nullptr);
    IsrDelegates::SetPostHandler(&WakeDrainTask);

    TimerHandle_t timer = xTimerCreate("DmqTimers", pdMS_TO_TICKS(10), pdTRUE, nullptr,
                                       ProcessTimersCallback);
    xTimerStart(timer, 0);

    // Constructed here, in task context (the IsrDelegate registers itself
    // under an OS mutex), and never destroyed
    static App app;
    static Stats stats;
    static Thread logThread("LogThread");
    logThread.CreateThread();
    app.Start();

    // 4. Signal subscribers: one synchronous, one async to LogThread
    static ScopedConnection logConn = app.OnColourChanged.Connect(MakeDelegate(&LogColourChange));
    static ScopedConnection statsConn = app.OnColourChanged.Connect(
        MakeDelegate(&stats, &Stats::OnColourChanged, logThread));

    printf("\n1. Synchronous delegate, on [%s]\n", TaskName());
    auto add = MakeDelegate(&Add);
    printf("   Add(2, 3) = %d\n", add(2, 3));

    printf("\n2. Async delegate: SetColour(blue) queued to AppThread; the caller doesn't wait\n");
    MakeDelegate(&app, &App::SetColour, app.GetThread())(1);

    printf("\n3. Async-wait delegate: GetColourName() runs on AppThread, the caller waits\n");
    const char* name = MakeDelegate(&app, &App::GetColourName, app.GetThread(), WAIT_INFINITE)();
    printf("   [%s] colour is %s\n", TaskName(), name);

    printf("\nRunning: the LED blinks; press BOOT to change its colour\n");
}
