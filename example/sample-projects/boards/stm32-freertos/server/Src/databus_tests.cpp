/**
 * @file databus_tests.cpp
 * @brief Local DataBus publish/subscribe demo on STM32 FreeRTOS.
 *
 * Demonstrates the DataBus API using two topics — sensor data and actuator
 * state — published by a timer-driven publisher and received by two
 * subscribers: one dispatched asynchronously to its own thread, one
 * synchronous with Last Value Cache (LVC) enabled.
 *
 * No network transport is used; all traffic stays in-process.
 */
#include "DelegateMQ.h"
#include "extras/databus/DataBus.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "../../common/Sensor.h"
#include "../../common/Actuator.h"
#include <stdio.h>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace dmq::databus;

// newlib-nano disables %f; use these macros to pass float parts to a single printf
#define FI(v) ((int)(v))
#define FF(v) ((int)(((v) - (int)(v)) * 100))

// Mutex guarding ITM printf — prevents interleaved output from concurrent tasks
static SemaphoreHandle_t s_printMutex = nullptr;
#define SAFE_PRINTF(...) do { \
    xSemaphoreTake(s_printMutex, portMAX_DELAY); \
    printf(__VA_ARGS__); \
    xSemaphoreGive(s_printMutex); \
} while(0)

static const char* TOPIC_SENSOR   = "sensor/data";
static const char* TOPIC_ACTUATOR = "actuator/state";

// ============================================================================
// PUBLISHER — fires a timer on its own thread, publishes both topics
// ============================================================================
class DataBusPublisher
{
public:
    DataBusPublisher() : m_thread("DB_Pub"), m_sensor(1), m_actuator(1)
    {
        m_thread.CreateThread(std::chrono::milliseconds(2000));
    }

    ~DataBusPublisher()
    {
        m_timer.Stop();
        m_thread.ExitThread();
    }

    void Start(uint32_t intervalMs)
    {
        m_timerConn = m_timer.OnExpired.Connect(
            MakeTimerDelegate(this, &DataBusPublisher::Publish, m_thread));
        m_timer.Start(std::chrono::milliseconds(intervalMs));
        printf("[DB_Pub] Publishing every %lu ms (Task: %s)\n",
            (unsigned long)intervalMs, pcTaskGetName(NULL));
    }

private:
    void Publish()
    {
        SensorData sd = m_sensor.GetSensorData();
        DataBus::Publish<SensorData>(TOPIC_SENSOR, sd);

        ActuatorState as = m_actuator.GetState();
        DataBus::Publish<ActuatorState>(TOPIC_ACTUATOR, as);

        BSP_LED_Toggle(LED6);
    }

    Thread              m_thread;
    Timer               m_timer;
    dmq::ScopedConnection m_timerConn;
    Sensor              m_sensor;
    Actuator            m_actuator;
};

// ============================================================================
// SUBSCRIBER A — async dispatch to its own thread
// ============================================================================
class DataBusSubscriberA
{
public:
    DataBusSubscriberA() : m_thread("DB_SubA")
    {
        m_thread.CreateThread(std::chrono::milliseconds(2000));

        m_sensorConn = DataBus::Subscribe<SensorData>(TOPIC_SENSOR,
            [](const SensorData& sd) {
                SAFE_PRINTF("[SubA] sensor id=%d  supply=%d.%02d  reading=%d.%02d  (Task: %s)\n",
                    sd.id, FI(sd.supplyV), FF(sd.supplyV),
                    FI(sd.readingV), FF(sd.readingV), pcTaskGetName(NULL));
            }, &m_thread);

        m_actuatorConn = DataBus::Subscribe<ActuatorState>(TOPIC_ACTUATOR,
            [](const ActuatorState& as) {
                SAFE_PRINTF("[SubA] actuator id=%d  pos=%d  volt=%d.%02d\n",
                    as.id, (int)as.position, FI(as.voltage), FF(as.voltage));
            }, &m_thread);
    }

    ~DataBusSubscriberA() { m_thread.ExitThread(); }

private:
    Thread              m_thread;
    dmq::ScopedConnection m_sensorConn;
    dmq::ScopedConnection m_actuatorConn;
};

// ============================================================================
// SUBSCRIBER B — synchronous, LVC enabled (gets last value on subscribe)
// ============================================================================
class DataBusSubscriberB
{
public:
    DataBusSubscriberB()
    {
        QoS qos;
        qos.lastValueCache = true;

        m_sensorConn = DataBus::Subscribe<SensorData>(TOPIC_SENSOR,
            [](const SensorData& sd) {
                SAFE_PRINTF("[SubB] sensor id=%d  reading=%d.%02d  (sync+LVC)\n",
                    sd.id, FI(sd.readingV), FF(sd.readingV));
            }, nullptr, qos);
    }

private:
    dmq::ScopedConnection m_sensorConn;
};

// ============================================================================
// TEST ENTRY POINT
// ============================================================================
void StartDatabusTests()
{
    s_printMutex = xSemaphoreCreateMutex();
    printf("\n=== STARTING DATABUS TESTS ===\n");

    // pub and subA live for the lifetime of this function (never returns),
    // so their threads remain registered with the watchdog indefinitely.
    DataBusPublisher pub;
    DataBusSubscriberA subA;
    pub.Start(500);

    vTaskDelay(pdMS_TO_TICKS(1500));

    // SubB joins mid-stream; LVC delivers last sensor value immediately on connect
    printf("\n[DataBus] SubB subscribing (LVC) -- last value delivered on connect\n");
    {
        DataBusSubscriberB subB;
        vTaskDelay(pdMS_TO_TICKS(1500));
        printf("\n[DataBus] SubB unsubscribing (RAII)\n");
    }

    // Publisher and SubA continue running indefinitely — watchdog monitors both threads
    printf("\n[DataBus] Running indefinitely. Watchdog active on DB_Pub and DB_SubA.\n");
    BSP_LED_On(LED4);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
