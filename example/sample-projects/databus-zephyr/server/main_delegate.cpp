/// @file main_delegate.cpp
/// @brief Zephyr DataBus server entry point (native_sim).
///
/// Starts a periodic k_timer to drive DelegateMQ's Timer subsystem, then
/// starts the Server active object and runs its publish loop. Mirrors
/// `databus-freertos/server/main.cpp`, adapted for Zephyr: no heap region
/// setup or FreeRTOS application hooks are needed, and main() runs directly
/// on Zephyr's own automatically-created main thread once the kernel is up.
///
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.

#include "DelegateMQ.h"
#include "Server.h"

#include <zephyr/kernel.h>
#include <cstdio>
#include <cstdlib>

static void SystemTimerHandler(struct k_timer* /*timer*/) {
    dmq::util::Timer::ProcessTimers();
}
K_TIMER_DEFINE(g_systemTimer, SystemTimerHandler, nullptr);

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // Tick the DelegateMQ Timer subsystem every 10 ms.
    k_timer_start(&g_systemTimer, K_MSEC(10), K_MSEC(10));

    printf("--- Starting Zephyr DataBus Server (native_sim) ---\n");

    if (!Server::GetInstance().Start())
    {
        printf("[Server] Startup failed.\n");
        exit(-1);
    }

    // Run blocks in a sleep loop until Stop() is called (never, in this demo --
    // matches databus-freertos/server, which also runs until the process is killed).
    Server::GetInstance().Run();

    Server::GetInstance().Stop();

    // native_sim runs as an ordinary Linux process -- exit() tears down every
    // thread (the poll thread, everything) in one step, the same approach
    // zephyr-linux/zephyr-udp-serializer use.
    exit(0);
}
