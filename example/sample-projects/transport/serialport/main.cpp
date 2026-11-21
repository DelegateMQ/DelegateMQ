/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Serial port with remote delegates examples.
///
/// Usage: delegate_app [seconds] [sender-port] [receiver-port]
///   seconds        how long the sender sends (default 5)
///   sender-port    default COM1
///   receiver-port  default COM2
/// The two ports must be connected to each other: a null-modem cable, a
/// virtual port pair (com0com on Windows), or on Linux a socat pty pair:
///   socat pty,raw,echo=0,link=/tmp/ttyDMQ0 pty,raw,echo=0,link=/tmp/ttyDMQ1
///   delegate_app 5 /tmp/ttyDMQ0 /tmp/ttyDMQ1
/// Exits with status 0 if the receiver got at least one message, else 1.

#include "sender.h"
#include <iostream>
#include "receiver.h"
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

int main(int argc, char* argv[])
{
    const int seconds = (argc > 1) ? std::atoi(argv[1]) : 5;
    const char* senderPort = (argc > 2) ? argv[2] : "COM1";
    const char* receiverPort = (argc > 3) ? argv[3] : "COM2";
    const DelegateRemoteId id = 1;

    std::cout << "Sender on " << senderPort << ", receiver on " << receiverPort << std::endl;

    // Start the thread that will run ProcessTimers
    std::thread timerThread(ProcessTimers);

    // Create Sender and Receiver objects
    Receiver receiver(id, receiverPort);
    Sender sender(id, senderPort);

    sender.Start();
    receiver.Start();

    // Let sender and receiver communicate
    this_thread::sleep_for(chrono::seconds(seconds > 0 ? seconds : 5));

    receiver.Stop();
    sender.Stop();

    // Ensure the timer thread completes before main exits
    processTimerExit.store(true);
    if (timerThread.joinable())
        timerThread.join();

    const int received = receiver.GetReceivedCount();
    std::cout << "Receiver got " << received << " message(s)" << std::endl;
    return received > 0 ? 0 : 1;
}
