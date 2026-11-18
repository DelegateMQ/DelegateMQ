/// @file main.cpp
/// @brief Linux/Windows DataBus client entry point.
///
/// Starts the Client, then periodically sends a CmdMsg to toggle the
/// FreeRTOS server's publish interval between 500 ms and 2000 ms.
///
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.

#include "DelegateMQ.h"
#include "Client.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

static std::atomic<bool> g_running(true);

static void SignalHandler(int) { g_running = false; }

int main(int argc, char* argv[])
{
    int duration = 0;
    if (argc > 1) duration = std::atoi(argv[1]);

    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::cout << "Starting DataBus FreeRTOS CLIENT...\n";

#ifdef _WIN32
    dmq::util::NetworkContext winsock;
#endif

    Client client;
    if (!client.Start())
        return -1;

    if (duration > 0)
    {
        std::thread([duration]() {
            std::this_thread::sleep_for(std::chrono::seconds(duration));
            g_running = false;
        }).detach();
    }
    else
    {
        std::cout << "Sending rate commands every 5s... Press Ctrl+C to quit.\n";
    }

    // Main loop: toggle the server's publish interval every 5 seconds
    int toggle = 0;
    auto lastCmd = std::chrono::steady_clock::now();

    while (g_running)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastCmd).count();

        if (elapsed >= 5)
        {
            // Alternate between fast (500 ms) and slow (2000 ms)
            int intervalMs = (toggle++ % 2 == 0) ? 500 : 2000;
            client.SendCmd(intervalMs);
            lastCmd = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nShutting down...\n";
    client.Stop();
    return 0;
}
