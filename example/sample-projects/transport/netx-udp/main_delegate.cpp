/// @file main_delegate.cpp
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Remote delegates over NetX Duo UDP (NetXUdpTransport) on ThreadX's
/// Linux simulation port.
///
/// The app is transport/serializer's (sender.h, receiver.h, the built-in
/// serializer's data.h): a Sender calls a remote delegate every 50 ms and a
/// Receiver gets it through NetX Duo, both in this process, over NetX's
/// loopback interface (127.0.0.1). Only the start-up differs: ThreadX threads,
/// a NetX packet pool and IP instance, and a thread that drives DelegateMQ's
/// timers. Runs for RUN_SECONDS, then exits with status 0 if messages arrived
/// intact and the receiver acknowledged them, else 1.

#include "sender.h"
#include "receiver.h"
#include "nx_api.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace dmq;
using namespace dmq::util;

// ThreadX's tick rate is TX_TIMER_TICKS_PER_SECOND (100 Hz: 10 ms per tick)
#define TX_MS_TO_TICKS(ms) ((ms) / (1000UL / TX_TIMER_TICKS_PER_SECOND))

static const int RUN_SECONDS = 5;

// NetX Duo: one IP instance on the RAM network driver (NetX's own test
// driver; no host network involved) and its packet pool. Traffic to 127.0.0.1
// goes through NetX's loopback interface.
NX_IP g_netxIp;
NX_PACKET_POOL g_netxPool;
static const ULONG PACKET_PAYLOAD = 1536;
static UCHAR g_poolMemory[64 * (PACKET_PAYLOAD + sizeof(NX_PACKET))];
static UCHAR g_ipStack[16384];
extern "C" VOID _nx_ram_network_driver(NX_IP_DRIVER* driver_req_ptr);

// The thread that drives DelegateMQ's timers (the Sender's send timer, the
// Receiver's poll timer, the retry monitor). A ThreadX timer callback isn't
// thread context and Timer::ProcessTimers() takes OS mutexes, so the callback
// only signals this thread.
static TX_TIMER g_systemTimer;
static TX_SEMAPHORE g_timerTick;
static TX_THREAD g_timerThread;
static UCHAR g_timerThreadStack[16384];

static TX_THREAD g_mainThread;
static UCHAR g_mainThreadStack[65536];

static void SystemTimerCallback(ULONG)
{
    tx_semaphore_ceiling_put(&g_timerTick, 1);
}

static void TimerThreadEntry(ULONG)
{
    while (true) {
        tx_semaphore_get(&g_timerTick, TX_WAIT_FOREVER);
        Timer::ProcessTimers();
    }
}

static bool StartNetX()
{
    nx_system_initialize();

    UINT status = nx_packet_pool_create(&g_netxPool, const_cast<CHAR*>("Pool"),
        PACKET_PAYLOAD, g_poolMemory, sizeof(g_poolMemory));
    if (status != NX_SUCCESS) {
        printf("nx_packet_pool_create failed: 0x%x\n", status);
        return false;
    }

    status = nx_ip_create(&g_netxIp, const_cast<CHAR*>("IP"), IP_ADDRESS(10, 0, 0, 1), 0xFFFFFF00UL,
        &g_netxPool, _nx_ram_network_driver, g_ipStack, sizeof(g_ipStack), 1);
    if (status != NX_SUCCESS) {
        printf("nx_ip_create failed: 0x%x\n", status);
        return false;
    }

    status = nx_udp_enable(&g_netxIp);
    if (status != NX_SUCCESS) {
        printf("nx_udp_enable failed: 0x%x\n", status);
        return false;
    }
    return true;
}

static void MainThreadEntry(ULONG)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!StartNetX())
        std::exit(1);

    const DelegateRemoteId id = 1;
    std::unique_ptr<Receiver> receiver = std::make_unique<Receiver>(id);
    std::unique_ptr<Sender> sender = std::make_unique<Sender>(id);

    sender->Start();
    receiver->Start();

    tx_thread_sleep(TX_MS_TO_TICKS(RUN_SECONDS * 1000UL));

    receiver->Stop();
    sender->Stop();

    const int received = receiver->ReceivedCount();
    const int acked = sender->AckedCount();
    std::cout << "Received " << received << " messages" << std::endl;
    std::cout << "Acknowledged " << acked << " messages, " << sender->TimeoutCount() << " timeouts" << std::endl;

    // ThreadX keeps running after this thread returns, so end the process here
    std::exit((received > 0 && acked > 0) ? 0 : 1);
}

extern "C" void main_delegate(void* /*first_unused_memory*/)
{
    tx_semaphore_create(&g_timerTick, const_cast<CHAR*>("TimerTick"), 0);
    tx_thread_create(&g_timerThread, const_cast<CHAR*>("TimerThread"), TimerThreadEntry, 0,
        g_timerThreadStack, sizeof(g_timerThreadStack), 2, 2, TX_NO_TIME_SLICE, TX_AUTO_START);
    tx_timer_create(&g_systemTimer, const_cast<CHAR*>("SysTimer"), SystemTimerCallback,
        0, 1, 1, TX_AUTO_ACTIVATE);
    tx_thread_create(&g_mainThread, const_cast<CHAR*>("MainThread"), MainThreadEntry, 0,
        g_mainThreadStack, sizeof(g_mainThreadStack), 16, 16, TX_NO_TIME_SLICE, TX_AUTO_START);
}
