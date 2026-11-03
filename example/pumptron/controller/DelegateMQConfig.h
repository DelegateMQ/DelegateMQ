#ifndef _DELEGATEMQ_CONFIG_H
#define _DELEGATEMQ_CONFIG_H

// Route every printf (app + DelegateMQ library) through a FreeRTOS mutex.
#include "util/Logger.h"

// Pumptron controller node -- shared by the FreeRTOS simulator and the
// STM32F4 Discovery targets. Limits sized for the F4 (192 KB RAM).

#define DMQ_DEFAULT_DISPATCH_TIMEOUT    2
#define DMQ_MAX_TIMER_EXPIRED           8
#define DMQ_SIGNAL_SBO_COUNT            4
#define DMQ_DEFAULT_QUEUE_SIZE          16
#define DMQ_MAX_WATCHDOG_THREADS        6
#define DMQ_SEQ_HISTORY_SIZE            4
#define DMQ_MAX_PARTICIPANTS            4
#define DMQ_TRANSPORT_MONITOR_MAX_PENDING 16
#define DMQ_NETWORK_NODE_MAX_PEERS      1
#define DMQ_NETWORK_NODE_MAX_TOPICS     8

// Unique per Pumptron node (controller 1, gui 2): receivers filter retried
// duplicates per (node ID, remote ID).
#define DMQ_NODE_ID                     1

#endif // _DELEGATEMQ_CONFIG_H
