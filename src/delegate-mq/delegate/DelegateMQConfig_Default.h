#ifndef _DELEGATEMQ_CONFIG_DEFAULT_H
#define _DELEGATEMQ_CONFIG_DEFAULT_H

/// @file
/// @brief Default DelegateMQ library configuration.
///
/// To override, define DMQ_USER_CONFIG as a path to your own config header:
///   -DDMQ_USER_CONFIG="path/to/DelegateMQConfig.h"
/// Your config only needs to define the values you want to change.
/// Unset values fall back to these defaults via DelegateMQConfig_Default.h.

#ifndef DMQ_DEFAULT_DISPATCH_TIMEOUT
    #define DMQ_DEFAULT_DISPATCH_TIMEOUT    2       // seconds
#endif

#ifndef DMQ_MAX_TIMER_EXPIRED
    #define DMQ_MAX_TIMER_EXPIRED           16
#endif

#ifndef DMQ_SIGNAL_SBO_COUNT
    #define DMQ_SIGNAL_SBO_COUNT            8
#endif

#ifndef DMQ_DEFAULT_QUEUE_SIZE
    #define DMQ_DEFAULT_QUEUE_SIZE          20
#endif

#ifndef DMQ_MAX_WATCHDOG_THREADS
    #define DMQ_MAX_WATCHDOG_THREADS        16
#endif

#ifndef DMQ_SEQ_HISTORY_SIZE
    #define DMQ_SEQ_HISTORY_SIZE            8
#endif

#ifndef DMQ_MAX_PARTICIPANTS
    #define DMQ_MAX_PARTICIPANTS            8
#endif

#ifndef DMQ_TRANSPORT_MONITOR_MAX_PENDING
    #define DMQ_TRANSPORT_MONITOR_MAX_PENDING 100
#endif

// Maximum number of distinct fixed-block allocator buckets (heap-blocks mode only).
// Each unique power-of-two allocation size gets one bucket. Most applications use
// 8-12 distinct sizes; raise this only if xallocator's ASSERT fires at runtime.
#ifndef DMQ_XALLOCATOR_MAX_ALLOCATORS
    #define DMQ_XALLOCATOR_MAX_ALLOCATORS   15
#endif

#endif // _DELEGATEMQ_CONFIG_DEFAULT_H
