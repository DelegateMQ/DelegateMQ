/// @file main.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.
///
/// @brief Remote delegates over a transport and serializer chosen at build
/// time (CMake SAMPLE_TRANSPORT and SAMPLE_SERIALIZER; see README.md).
///
/// A Sender sends Data to a Receiver every 50 ms through a remote delegate,
/// both in this process. Usage: delegate_app [seconds] (default 5). Exits
/// with status 1 if no message arrived intact, or, with acknowledgments
/// (SAMPLE_ACKS), if none was acknowledged.

#include "sender.h"
#include "receiver.h"
#include "extras/util/NetworkConnect.h"
#include <memory>

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
    int duration = 5;
    if (argc > 1) duration = atoi(argv[1]);

    // Starts Winsock on Windows (cleaned up when main exits); nothing elsewhere
    NetworkContext networkContext;

#if defined(SAMPLE_TRANSPORT_INIT) && SAMPLE_TRANSPORT_INIT
    // A transport whose stack must start first (lwip: lwIP's tcpip thread)
    SampleTransportInit();
#endif

    const DelegateRemoteId id = 1;

    // Start the thread that will run ProcessTimers
    std::thread timerThread(ProcessTimers);

    // Create the Sender and Receiver in the order the transport needs (a
    // server before its client; see SAMPLE_SENDER_FIRST in transport.h)
    std::unique_ptr<Sender> sender;
    std::unique_ptr<Receiver> receiver;
#if SAMPLE_SENDER_FIRST
    sender = std::make_unique<Sender>(id);
    receiver = std::make_unique<Receiver>(id);
#else
    receiver = std::make_unique<Receiver>(id);
    sender = std::make_unique<Sender>(id);
#endif

    sender->Start();
    receiver->Start();

    // Let sender and receiver communicate
    this_thread::sleep_for(chrono::seconds(duration));

    receiver->Stop();
    sender->Stop();

    // Ensure the timer thread completes before main exits
    processTimerExit.store(true);
    if (timerThread.joinable())
        timerThread.join();

    const int received = receiver->ReceivedCount();
    cout << "Received " << received << " messages" << endl;
#if SAMPLE_ACKS
    // With acknowledgments, the sender must also have heard back: a lost
    // ACK path otherwise shows only as timeouts and retries.
    const int acked = sender->AckedCount();
    cout << "Acknowledged " << acked << " messages, " << sender->TimeoutCount() << " timeouts" << endl;
    return (received > 0 && acked > 0) ? 0 : 1;
#else
    return received > 0 ? 0 : 1;
#endif
}
