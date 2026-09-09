/// @file main_delegate.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Remote delegate demo over Zephyr's native UDP transport
/// (native_sim). A Sender and Receiver both run in this one process,
/// exchanging a real UDP packet over the loopback interface (127.0.0.1)
/// rather than calling each other directly -- exercising ZephyrUdpTransport
/// end-to-end (socket(), sendto()/recvfrom(), header framing) instead of
/// just DelegateMQ's in-process delegate/thread mechanics.
///
/// sender.h/receiver.h are copied unchanged from linux-udp-serializer:
/// both reference dmq::transport::UdpTransport generically, which resolves
/// to ZephyrUdpTransport here purely from DMQ_TRANSPORT_ZEPHYR_UDP at
/// compile time -- the same portability the dmq::os::Thread alias already
/// gives every DMQ_THREAD_* port.

#include "DelegateMQ.h"
#include "sender.h"
#include "receiver.h"
#include <zephyr/kernel.h>
#include <cstdio>
#include <cstdlib>

using namespace dmq;
using namespace dmq::util;

static void SystemTimerHandler(struct k_timer* /*timer*/) {
    Timer::ProcessTimers();
}
K_TIMER_DEFINE(g_systemTimer, SystemTimerHandler, nullptr);

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    // Periodic software timer drives DelegateMQ's Timer::ProcessTimers(),
    // which both Sender and Receiver use to schedule their send/poll ticks.
    k_timer_start(&g_systemTimer, K_MSEC(10), K_MSEC(10));

    printf("\n=========================================\n");
    printf("   ZEPHYR UDP REMOTE DELEGATE DEMO        \n");
    printf("=========================================\n\n");

    const DelegateRemoteId id = 1;

    // Receiver binds (SUB) to 127.0.0.1:8080 first; Sender (PUB) targets
    // that same address. Both sockets exist before either timer's first
    // tick, so construction order here doesn't race real traffic.
    Receiver receiver(id);
    Sender sender(id);

    sender.Start();
    receiver.Start();

    printf("Exchanging data over UDP loopback (127.0.0.1:8080) for 3 seconds...\n\n");
    k_sleep(K_SECONDS(3));

    receiver.Stop();
    sender.Stop();

    printf("\n=========================================\n");
    printf("           DEMO COMPLETE                  \n");
    printf("=========================================\n");

    // Zephyr has no kernel-level "stop scheduler" call, and native_sim runs
    // as an ordinary Linux process -- exit() tears down every thread in the
    // process in one step, the same approach zephyr-linux uses. Returning
    // from main() normally hits a native_sim/posix-arch thread-table
    // teardown edge case instead.
    exit(0);
}
