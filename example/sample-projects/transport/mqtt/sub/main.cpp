/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief MQTT and RapidJSON with remote delegates examples.

#include "receiver.h"
#include <iostream>
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

static std::atomic<int> received{0};

void DataCb(Data& data)
{
    if (data.dataPoints.size() > 0) {
        cout << data.msg << " " << data.dataPoints[0].y << " " << data.dataPoints[0].y << endl;
        received++;
    }
    else
        cout << "DataUpdate incoming data error!" << endl;
}

// Usage: delegate_sub_app [broker-uri] [seconds]
//   broker-uri  default tcp://localhost:1883
//   seconds     how long to receive (default 60)
// Exits with status 0 if it received at least one message, else 1.
int main(int argc, char* argv[])
{
    const char* broker = (argc > 1) ? argv[1] : MqttTransport::DEFAULT_ADDRESS;
    const int seconds = (argc > 2 && std::atoi(argv[2]) > 0) ? std::atoi(argv[2]) : 60;

    // Start the thread that will run ProcessTimers
    std::thread timerThread(ProcessTimers);

    Thread recvThread("RecvThread");
    recvThread.CreateThread();

    // Create Receiver object
    Receiver receiver(broker);
    receiver.DataCb += dmq::MakeDelegate(&DataCb, recvThread);

    receiver.Start();

    // Let sender and receiver communicate
    this_thread::sleep_for(chrono::seconds(seconds));

    receiver.Stop();

    // Ensure the timer thread completes before main exits
    processTimerExit.store(true);
    if (timerThread.joinable())
        timerThread.join();

    cout << "Received " << received.load() << " message(s)" << endl;
    return received.load() > 0 ? 0 : 1;
}