// DelegateMQ PlatformIO example: one sketch for every board in platformio.ini.
//
// All boards: synchronous delegates and a Signal with a ScopedConnection.
// Boards with threads (ESP32, desktop): asynchronous delegates on a worker
// thread, an URGENT message that runs ahead of queued NORMAL ones, and a
// blocking async-wait call that returns a value from the worker thread.

#include "DelegateMQ.h"
#include <cstdarg>
#include <cstdio>

#ifdef ARDUINO
#include <Arduino.h>
#endif

// ---------------------------------------------------------------------------
// Output: Serial on Arduino, stdout on the desktop
// ---------------------------------------------------------------------------
static void Log(const char* fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
#ifdef ARDUINO
    Serial.println(buf);
#else
    puts(buf);
#endif
}

// ---------------------------------------------------------------------------
// Bare metal: DelegateMQ's clock reads a millisecond tick the application
// provides. On Arduino, millis() is that tick.
// ---------------------------------------------------------------------------
#if defined(DMQ_THREAD_NONE)
extern "C" volatile uint64_t g_ticks = 0;
static void UpdateTicks()
{
#ifdef ARDUINO
    g_ticks = millis();
#endif
}
#else
static void UpdateTicks() {}
#endif

// ---------------------------------------------------------------------------
// Demo code
// ---------------------------------------------------------------------------
static void OnTemperature(int celsius)
{
    Log("  OnTemperature: %d C", celsius);
}

class Sensor
{
public:
    // Anyone can connect; Signal holds the subscribers.
    dmq::Signal<void(int)> OnReading;

    void Sample(int value) { OnReading(value); }
};

class Display
{
public:
    explicit Display(Sensor& sensor)
    {
        // The connection lives as long as m_conn, so store it.
        m_conn = sensor.OnReading.Connect(dmq::MakeDelegate(this, &Display::Show));
    }

private:
    void Show(int value) { Log("  Display::Show: %d", value); }
    dmq::ScopedConnection m_conn;
};

static void RunSyncDemo()
{
    Log("Synchronous delegates");
    auto d = dmq::MakeDelegate(&OnTemperature);
    d(21);

    Log("Signal");
    Sensor sensor;
    {
        Display display(sensor);
        sensor.Sample(42);      // Display receives it
    }
    sensor.Sample(43);          // Display is gone: connection closed, nothing runs
}

#if defined(DMQ_HAS_THREADING)
static dmq::os::Thread worker("Worker");

static void Task(int id)
{
    Log("  Task %d", id);
}

static int Add(int a, int b)
{
    return a + b;
}

static void RunAsyncDemo()
{
    Log("Asynchronous delegates on thread '%s'", "Worker");
    worker.CreateThread();

    // Block the worker briefly so the next messages queue up behind this one.
    auto busy = dmq::MakeDelegate(+[](int ms) {
        auto start = dmq::Clock::now();
        while (dmq::Clock::now() - start < std::chrono::milliseconds(ms)) {}
        Log("  Busy %d ms done", ms);
    }, worker);
    busy(100);

    auto normal = dmq::MakeDelegate(&Task, worker);
    for (int i = 1; i <= 3; i++)
        normal(i);

    // URGENT runs ahead of the NORMAL tasks still waiting in the queue.
    auto urgent = dmq::MakeDelegate(&Task, worker);
    urgent.SetPriority(dmq::Priority::URGENT);
    urgent(99);

    // Async-wait: runs Add() on the worker and blocks until it returns.
    auto add = dmq::MakeDelegate(&Add, worker, dmq::WAIT_INFINITE);
    int sum = add(2, 3);
    Log("  Add(2, 3) on Worker returned %d", sum);
}
#endif

static void RunDemo()
{
    Log("DelegateMQ PlatformIO example");
    RunSyncDemo();
#if defined(DMQ_HAS_THREADING)
    RunAsyncDemo();
#else
    Log("No thread port on this board: async demo skipped");
#endif
    Log("Done");
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------
#ifdef ARDUINO
void setup()
{
    Serial.begin(115200);
    delay(2000);    // time to open the serial monitor
    UpdateTicks();
    RunDemo();
}

void loop()
{
    UpdateTicks();
    delay(10);
}
#else
int main()
{
    RunDemo();
#if defined(DMQ_HAS_THREADING)
    worker.ExitThread();
#endif
    return 0;
}
#endif
