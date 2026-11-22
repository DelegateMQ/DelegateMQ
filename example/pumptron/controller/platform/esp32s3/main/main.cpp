/**
 * @file controller/platform/esp32s3/main/main.cpp
 * @brief Pumptron controller -- ESP32-S3-DevKitC-1 target (ESP-IDF FreeRTOS, Wi-Fi).
 *
 * Runs the exact same PumpController as the simulator and STM32F4 targets,
 * with two substitutions made here and nowhere else:
 *   - Esp32Board (chip temperature sensor, BOOT button, RGB LED) instead of
 *     SimBoard/F4Board
 *   - NetworkNode<UDP> over Wi-Fi (lwIP sockets), the simulator's link, with
 *     the GUI on the PC at CONFIG_APP_GUI_ADDRESS instead of localhost
 *
 * Start the GUI with `--udp <this board's address>` (printed at startup).
 *
 * Threads and cores (the ESP32-S3 has two; dmq::os::Thread::SetCoreAffinity()
 * pins a thread to one, via xTaskCreatePinnedToCore()):
 *   - Core 1: "Pump" (PumpController), the 20 Hz control loop. Nothing else of
 *     the application runs there, so Wi-Fi bursts never delay a control tick.
 *   - Core 0: "Controller_NetworkThread" (NetworkNode: receives, and sends to
 *     the GUI), next to ESP-IDF's Wi-Fi and TCP/IP tasks, which it talks to.
 *   - Core 0: this task, app_main ("main"): Timer::ProcessTimers() every 10 ms
 *     (drives the pump's control and heartbeat timers and the link's receive
 *     poll), the thread watchdog, and the ESP-IDF task watchdog (TWDT).
 * printf() is ESP-IDF's (UART console, 115200).
 */

#include "DelegateMQ.h"
#include "extras/databus/NetworkNode.h"
#include "pump/PumpController.h"
#include "pump/LinkErrorReporter.h"
#include "util/Topology.h"
#include "Esp32Board.h"
#include "Wifi.h"

#include "sdkconfig.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstdlib>

using namespace pumptron;
using ControllerLink = dmq::databus::NetworkNode<dmq::transport::UdpTransport>;

namespace {

/// Core masks for dmq::os::Thread::SetCoreAffinity(): bit 0 = core 0, bit 1 = core 1
constexpr uint32_t NET_CORE_MASK  = 1u << 0;
constexpr uint32_t PUMP_CORE_MASK = 1u << 1;

constexpr uint32_t WIFI_TIMEOUT_MS = 30000;
constexpr uint32_t WATCHDOG_CHECK_TICKS = 50;      // x TIMER_TICK_PERIOD (10 ms) = 500 ms
constexpr uint32_t TASK_LIST_TICKS = 100;          // print the task table once, after 1 s

/// Every task, its priority and its core: shows where SetCoreAffinity() put
/// the application's threads among ESP-IDF's own tasks.
void PrintTasks()
{
    constexpr UBaseType_t MAX_TASKS = 32;
    static TaskStatus_t tasks[MAX_TASKS];
    const UBaseType_t n = uxTaskGetSystemState(tasks, MAX_TASKS, nullptr);
    printf("\n%-16s %4s %5s %10s\n", "Task", "Prio", "Core", "Stack free");
    for (UBaseType_t i = 0; i < n; ++i) {
        char core[12];  // "any", or a core number (an int, for -Wformat-truncation)
        if (tasks[i].xCoreID == tskNO_AFFINITY)
            snprintf(core, sizeof(core), "any");
        else
            snprintf(core, sizeof(core), "%d", static_cast<int>(tasks[i].xCoreID));
        printf("%-16s %4u %5s %10u\n", tasks[i].pcTaskName,
               static_cast<unsigned>(tasks[i].uxCurrentPriority), core,
               static_cast<unsigned>(tasks[i].usStackHighWaterMark));
    }
    printf("\n");
}

} // namespace

extern "C" void app_main()
{
    printf("\nPumptron controller starting (ESP32-S3)...\n");
    if (!esp32s3::StartWifi(WIFI_TIMEOUT_MS)) {
        printf("Wi-Fi: not connected. Check the SSID and password in menuconfig.\n");
        return;
    }

    // Built here, after Wi-Fi: dmq::os::Thread and the UDP sockets need the
    // scheduler and lwIP running (app_main is already a FreeRTOS task).
    static board::Esp32Board board;
    static pump::PumpController pumpController(board);
    static ControllerLink link;

    // DataBus + link errors -> LINK_DEGRADED alarm / status resync.
    static pump::LinkErrorReporter errorReporter(link, pumpController);

    ConfigureControllerLink(link);
    link.SetCoreAffinity(NET_CORE_MASK);
    if (!link.Start("Controller", CONTROLLER_UDP_PORT)) {
        printf("Controller: ERROR - could not open UDP port %u\n", CONTROLLER_UDP_PORT);
        abort();
    }
    link.AddPeer("GUI", CONFIG_APP_GUI_ADDRESS, GUI_UDP_PORT);

    pumpController.GetThread().SetThreadPriority(PRIORITY_PUMP);
    pumpController.GetThread().SetCoreAffinity(PUMP_CORE_MASK);
    pumpController.Start(WATCHDOG_TIMEOUT);

    printf("Pumptron controller (ESP32-S3) at %s, UDP %u; GUI at %s:%u\n",
           esp32s3::WifiAddress(), CONTROLLER_UDP_PORT, CONFIG_APP_GUI_ADDRESS, GUI_UDP_PORT);
    printf("On the PC: pumptron_gui --udp %s\n", esp32s3::WifiAddress());

    // The platform loop. Above the application's threads, so a busy pump
    // thread can't starve the timers and watchdogs it depends on.
    vTaskPrioritySet(nullptr, PRIORITY_LINK + 1);

    // ESP-IDF's task watchdog (TWDT) watches this loop, as the F4's IWDG does:
    // if it stops (hung, starved), the TWDT panics and resets the board.
    esp_task_wdt_add(nullptr);

    const TickType_t period = pdMS_TO_TICKS(
        std::chrono::duration_cast<std::chrono::milliseconds>(TIMER_TICK_PERIOD).count());
    TickType_t lastWake = xTaskGetTickCount();
    for (uint32_t tick = 1; ; ++tick) {
        dmq::util::Timer::ProcessTimers();
        if (tick % WATCHDOG_CHECK_TICKS == 0) {
            dmq::os::Thread::WatchdogCheckAll();
            esp_task_wdt_reset();
        }
        if (tick == TASK_LIST_TICKS)
            PrintTasks();
        vTaskDelayUntil(&lastWake, period);
    }
}
