/**
 * @file controller/main_threadx.cpp
 * @brief Controller CPU — Process Sequencing & Hardware Control (ThreadX build)
 *
 * ThreadX equivalent of main.cpp. Everything below this file -- Process.h,
 * System.h, Actuators, Sensors, the state machines -- is untouched and
 * identical to the FreeRTOS build; only the kernel init/task-creation glue
 * here differs. See main.cpp for the FreeRTOS version of this same glue.
 */

#include "DelegateMQ.h"
#include "system/System.h"
#include "extras/util/NetworkConnect.h"
#include "tx_api.h"
#include <cstdio>

using namespace dmq;
using namespace dmq::os;
using namespace dmq::util;
using namespace cellutron;

// ThreadX's default tick rate is TX_TIMER_TICKS_PER_SECOND (100 Hz == 10ms/tick).
#define TX_MS_TO_TICKS(ms) ((ms) / (1000UL / TX_TIMER_TICKS_PER_SECOND))

#define CONTROLLER_STACK_SIZE 65536
#define WATCHDOG_STACK_SIZE   16384

static TX_TIMER g_sysTimer;
static TX_THREAD g_controllerThread;
static TX_THREAD g_watchdogThread;
static UCHAR g_controllerStack[CONTROLLER_STACK_SIZE];
static UCHAR g_watchdogStack[WATCHDOG_STACK_SIZE];

// ---------------------------------------------------------------------------
// Tasks
// ---------------------------------------------------------------------------

static void OnUnhandledTopic(const dmq::xstring& topic) {
    printf("CONTROLLER WARNING: Unhandled topic: %s\n", topic.c_str());
}

static void OnTechnicalError(const dmq::xstring& topic, dmq::DelegateError error) {
    printf("CONTROLLER ERROR: Technical failure on topic: %s, Error: %d\n", topic.c_str(), (int)error);
}

static void ControllerThreadEntry(ULONG) {
    cellutron::System::GetInstance().Initialize();

    // Catch unhandled topics (sent but no subscribers)
    static auto unhandledConn = dmq::databus::DataBus::SubscribeUnhandled(dmq::MakeDelegate(&OnUnhandledTopic));

    // Catch technical errors (e.g. serialization failures)
    static auto errorConn = dmq::databus::DataBus::SubscribeError(dmq::MakeDelegate(&OnTechnicalError));

    for (;;) {
        cellutron::System::GetInstance().Tick(100);
        Thread::Sleep(std::chrono::milliseconds(100));
    }
}

static void WatchdogThreadEntry(ULONG) {
    for (;;) {
        Thread::WatchdogCheckAll();
        tx_thread_sleep(TX_MS_TO_TICKS(100));
    }
}

static void SysTimerCallback(ULONG) {
    dmq::util::Timer::ProcessTimers();
}

// ---------------------------------------------------------------------------
// ThreadX kernel entry — tx_kernel_enter() calls this once, before scheduling
// starts, to create all initial ThreadX objects.
// ---------------------------------------------------------------------------
extern "C" void tx_application_define(void* /*first_unused_memory*/) {
    ::InstallCrashHandlers();
    static dmq::util::NetworkContext networkContext;
    printf("Cellutron Controller Processor starting (ThreadX Simulator)...\n");

    // Priority 0 = highest urgency on ThreadX; matches the FreeRTOS build's
    // top-priority Controller/Watchdog tasks. See Constants.h for the app
    // subsystem threads' (System/Hardware/Network) priority mapping.
    tx_thread_create(&g_controllerThread, const_cast<CHAR*>("Controller"), ControllerThreadEntry, 0,
                      g_controllerStack, CONTROLLER_STACK_SIZE, 0, 0, TX_NO_TIME_SLICE, TX_AUTO_START);

    tx_thread_create(&g_watchdogThread, const_cast<CHAR*>("Watchdog"), WatchdogThreadEntry, 0,
                      g_watchdogStack, WATCHDOG_STACK_SIZE, 0, 0, TX_NO_TIME_SLICE, TX_AUTO_START);

    // Periodic software timer drives DelegateMQ's Timer::ProcessTimers().
    // 1 tick == 10ms at the default TX_TIMER_TICKS_PER_SECOND (100Hz).
    tx_timer_create(&g_sysTimer, const_cast<CHAR*>("SysTimer"), SysTimerCallback, 0, 1, 1, TX_AUTO_ACTIVATE);
}

int main(void) {
    printf("--- Starting ThreadX Kernel (Linux simulation) ---\n");

    /* Never returns. */
    tx_kernel_enter();

    return 0;
}
