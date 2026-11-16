/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief MQTT and RapidJSON with remote delegates examples. 

#include "sender.h"
#include <atomic>
#include <cstdlib>

std::atomic<bool> processTimerExit = false;
static void ProcessTimers()
{
    while (!processTimerExit.load())
    {
        // Process all delegate-based timers
        Timer::ProcessTimers();
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

// Usage: delegate_pub_app [broker-uri] [seconds]
//   broker-uri  default tcp://localhost:1883
//   seconds     how long to publish (default 60)
int main(int argc, char* argv[])
{
    const char* broker = (argc > 1) ? argv[1] : MqttTransport::DEFAULT_ADDRESS;
    const int seconds = (argc > 2 && std::atoi(argv[2]) > 0) ? std::atoi(argv[2]) : 60;

    // Start the thread that will run ProcessTimers
    std::thread timerThread(ProcessTimers);

    // Create Sender object
    Sender sender(broker);

    sender.Start();

    // Let sender and receiver communicate
    this_thread::sleep_for(chrono::seconds(seconds));

    sender.Stop();

    // Ensure the timer thread completes before main exits
    processTimerExit.store(true);
    if (timerThread.joinable())
        timerThread.join();

    return 0;
}