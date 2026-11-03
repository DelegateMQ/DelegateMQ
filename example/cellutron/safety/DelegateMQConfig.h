#ifndef _DELEGATEMQ_CONFIG_H
#define _DELEGATEMQ_CONFIG_H

#include "util/Logger.h"

// Cellutron Safety node — FreeRTOS or ThreadX target (see CELLUTRON_RTOS),
// minimal thread count. Reduced limits to constrain RAM on embedded hardware.

#define DMQ_DEFAULT_DISPATCH_TIMEOUT    2
#define DMQ_MAX_TIMER_EXPIRED           4
#define DMQ_SIGNAL_SBO_COUNT            8
#define DMQ_DEFAULT_QUEUE_SIZE          20
#define DMQ_MAX_WATCHDOG_THREADS        4
#define DMQ_SEQ_HISTORY_SIZE            4
#define DMQ_MAX_PARTICIPANTS            8
#define DMQ_ALLOCATOR_SAFEGUARDS

// Unique per Cellutron node (controller 1, safety 2, gui 3): receivers filter retried
// duplicates per (node ID, remote ID).
#define DMQ_NODE_ID                     2

#endif // _DELEGATEMQ_CONFIG_H
