#ifndef _DELEGATEMQ_CONFIG_H
#define _DELEGATEMQ_CONFIG_H

#include "util/Logger.h"

// Cellutron GUI node — desktop stdlib target, use library defaults.

#define DMQ_DEFAULT_DISPATCH_TIMEOUT    2
#define DMQ_MAX_TIMER_EXPIRED           16
#define DMQ_SIGNAL_SBO_COUNT            8
#define DMQ_DEFAULT_QUEUE_SIZE          20
#define DMQ_MAX_WATCHDOG_THREADS        16
#define DMQ_SEQ_HISTORY_SIZE            8
#define DMQ_MAX_PARTICIPANTS            8

#endif // _DELEGATEMQ_CONFIG_H
