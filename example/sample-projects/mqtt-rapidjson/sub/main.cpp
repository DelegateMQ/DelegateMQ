/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
/// 
/// @brief MQTT and RapidJSON with remote delegates examples.

#include "receiver.h"

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

void DataCb(Data& data)
{
    if (data.dataPoints.size() > 0)
        cout << data.msg << " " << data.dataPoints[0].y << " " << data.dataPoints[0].y << endl;
    else
        cout << "DataUpdate incoming data error!" << endl;
}

// Usage: delegate_sub_app [broker-uri]   (default: tcp://localhost:1883)
int main(int argc, char* argv[])
{
    const char* broker = (argc > 1) ? argv[1] : MqttTransport::DEFAULT_ADDRESS;

    // Start the thread that will run ProcessTimers
    std::thread timerThread(ProcessTimers);

    Thread recvThread("RecvThread");
    recvThread.CreateThread();

    // Create Receiver object
    Receiver receiver(broker);
    receiver.DataCb += dmq::MakeDelegate(&DataCb, recvThread);

    receiver.Start();

    // Let sender and receiver communicate
    this_thread::sleep_for(chrono::seconds(60));

    receiver.Stop();

    // Ensure the timer thread completes before main exits
    processTimerExit.store(true);
    if (timerThread.joinable())
        timerThread.join();

    return 0;
}