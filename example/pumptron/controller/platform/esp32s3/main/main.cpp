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
 *   - Core 0: "SpyReporter" (CONFIG_APP_SPY), low priority: sends every DataBus
 *     message to dmq-spy on the PC, as sender "Controller".
 *   - Core 0: "NodeReporter" (CONFIG_APP_MONITOR), low priority: heartbeat and
 *     link health to dmq-monitor, thread statistics to dmq-thread.
 *   - Any core: "ThreadMonitor" (CONFIG_APP_MONITOR): every 2 s, publishes
 *     the registered threads' queue depth and dispatch latency.
 *   - Core 0: this task, app_main ("main"): Timer::ProcessTimers() every 10 ms
 *     (drives the pump's control and heartbeat timers and the link's receive
 *     poll), the thread watchdog, and the ESP-IDF task watchdog (TWDT).
 * printf() is ESP-IDF's (UART console, 115200).
 */

#include "DelegateMQ.h"
#include "extras/databus/NetworkNode.h"
#include "extras/databus/SpyReporter.h"
#include "extras/databus/SpyLogSink.h"
#include "extras/util/LogSinks.h"
#include "extras/databus/NodeReporter.h"
#include "extras/util/ThreadMonitor.h"
#include "pump/PumpController.h"
#include "pump/LinkErrorReporter.h"
#include "util/Stringifiers.h"
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

/// The diagnostic reporters' threads: below the application's, so their
/// traffic waits and the pump never does
constexpr int PRIORITY_DIAGNOSTICS = 2;

#if CONFIG_APP_SPY

void OnSpyDropped(uint32_t total)
{
    // Rate-limited: the first drop, then every 100th
    if (total == 1 || total % 100 == 0)
        printf("SpyReporter: %lu messages dropped (queue full or send failed)\n",
               static_cast<unsigned long>(total));
}

/// Every message on this node's DataBus -> dmq-spy on the PC, as sender
/// "Controller", with this board's timestamps. Started before the link, so
/// the first messages are already labeled and sent.
/// @return The reporter's thread (for ThreadMonitor), or nullptr if not started.
dmq::os::Thread* StartSpy()
{
    static dmq::transport::UdpTransport transport;
    static dmq::databus::SpyReporter spy;
    static dmq::ScopedConnection droppedConn;

    RegisterStringifiers();     // values as text in dmq-spy
    if (transport.Create(dmq::transport::UdpTransport::Type::PUB, CONFIG_APP_GUI_ADDRESS, CONFIG_APP_SPY_PORT) != 0) {
        printf("SpyReporter: could not open UDP to %s:%d, dmq-spy off\n", CONFIG_APP_GUI_ADDRESS, CONFIG_APP_SPY_PORT);
        return nullptr;
    }
    droppedConn = spy.OnDropped.Connect(dmq::MakeDelegate(&OnSpyDropped));
    spy.GetThread().SetCoreAffinity(NET_CORE_MASK);
    spy.GetThread().SetThreadPriority(PRIORITY_DIAGNOSTICS);
    // Watched like the pump and the link: a spy thread stuck in Send() is a
    // hung thread (WatchdogCheckAll() in the platform loop)
    if (!spy.Start(transport, "Controller", WATCHDOG_TIMEOUT))
        return nullptr;
    // Log lines to dmq-spy too ("log/<level>/<tag>"), and still to the console
    dmq::databus::SpyLogSink::Attach(spy, &dmq::log::EspLogSink);
    printf("SpyReporter: DataBus traffic to dmq-spy at %s:%d\n", CONFIG_APP_GUI_ADDRESS, CONFIG_APP_SPY_PORT);
    return &spy.GetThread();
}
#endif

#if CONFIG_APP_MONITOR
void OnMonitorDropped(uint32_t total)
{
    if (total == 1 || total % 100 == 0)
        printf("NodeReporter: %lu messages dropped (queue full or send failed)\n",
               static_cast<unsigned long>(total));
}

/// This node to dmq-monitor (heartbeat, the link's health) and its threads to
/// dmq-thread (ThreadMonitor's statistics), on the multicast group both
/// consoles listen on. After the link and the pump have started.
void StartMonitor(ControllerLink& link, pump::PumpController& pumpController, dmq::os::Thread* spyThread)
{
    static dmq::transport::UdpTransport transport;
    static dmq::databus::NodeReporter reporter;
    static dmq::ScopedConnection droppedConn;

    if (transport.Create(dmq::transport::UdpTransport::Type::PUB, CONFIG_APP_MONITOR_GROUP, CONFIG_APP_MONITOR_PORT) != 0) {
        printf("NodeReporter: could not open UDP to %s:%d, dmq-monitor off\n",
               CONFIG_APP_MONITOR_GROUP, CONFIG_APP_MONITOR_PORT);
        return;
    }
    droppedConn = reporter.OnDropped.Connect(dmq::MakeDelegate(&OnMonitorDropped));
    reporter.Watch(link);                               // its peers' ACK timeouts, failures, backlogs
    reporter.GetThread().SetCoreAffinity(NET_CORE_MASK);
    reporter.GetThread().SetThreadPriority(PRIORITY_DIAGNOSTICS);
    if (!reporter.Start(transport, "Controller", esp32s3::WifiAddress(), "esp32s3",
                        dmq::NODE_REPORTER_HEARTBEAT, WATCHDOG_TIMEOUT))
        return;

    // The threads dmq-thread shows: queue depth and dispatch latency, every 2 s
    dmq::util::ThreadMonitor::Register(&pumpController.GetThread());
    dmq::util::ThreadMonitor::Register(link.GetThread());
    dmq::util::ThreadMonitor::Register(&reporter.GetThread());
    if (spyThread)
        dmq::util::ThreadMonitor::Register(spyThread);
    dmq::util::ThreadMonitor::Enable();
    printf("NodeReporter: to dmq-monitor and dmq-thread at %s:%d\n", CONFIG_APP_MONITOR_GROUP, CONFIG_APP_MONITOR_PORT);
}
#endif

} // namespace

extern "C" void app_main()
{
    // DMQ_LOG_* lines (the pump, the link, the library) to ESP-IDF's log; with
    // CONFIG_APP_SPY, StartSpy() adds dmq-spy
    dmq::SetLogHandler(&dmq::log::EspLogSink);
    printf("\nPumptron controller starting (ESP32-S3)...\n");
    if (!esp32s3::StartWifi(WIFI_TIMEOUT_MS)) {
        printf("Wi-Fi: not connected. Check the SSID and password in menuconfig.\n");
        return;
    }

    dmq::os::Thread* spyThread = nullptr;
#if CONFIG_APP_SPY
    spyThread = StartSpy();
#endif

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

#if CONFIG_APP_MONITOR
    StartMonitor(link, pumpController, spyThread);
#endif

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
