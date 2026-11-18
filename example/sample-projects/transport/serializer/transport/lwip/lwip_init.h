#ifndef LWIP_INIT_H
#define LWIP_INIT_H

/// @file lwip_init.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief Starts lwIP for the lwip and lwip-netconn builds (both use this
/// folder's lwipopts.h).

#include "lwip/tcpip.h"
#include "lwip/sys.h"

/// 1: main() calls SampleTransportInit() before creating the transports.
#define SAMPLE_TRANSPORT_INIT 1

/// Start lwIP: its tcpip thread, and with it the loopback interface
/// (127.0.0.1). Returns once lwIP is ready.
inline void SampleTransportInit()
{
    sys_sem_t ready;
    if (sys_sem_new(&ready, 0) != ERR_OK)
        return;
    tcpip_init([](void* arg) { sys_sem_signal(static_cast<sys_sem_t*>(arg)); }, &ready);
    sys_arch_sem_wait(&ready, 0);
    sys_sem_free(&ready);
}

#endif
