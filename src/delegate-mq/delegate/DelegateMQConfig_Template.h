#ifndef _DELEGATEMQ_CONFIG_H
#define _DELEGATEMQ_CONFIG_H

/// @file
/// @brief User configuration for the DelegateMQ library.
///
/// Copy this file to your project, then point the compiler at it:
///   -DDMQ_USER_CONFIG="DelegateMQConfig.h"
///
/// Only define the values you want to change; any omitted values
/// fall back to the defaults in DelegateMQConfig_Default.h.

/// Timeout (seconds) used by the TIMEOUT queue-full policy on all threads.
#define DMQ_DEFAULT_DISPATCH_TIMEOUT    2

/// Max timers processed in one tick without heap allocation.
#define DMQ_MAX_TIMER_EXPIRED           16

/// Signal Small-Buffer Optimization count.
/// Signals with <= this many subscribers are invoked without heap allocation.
#define DMQ_SIGNAL_SBO_COUNT            8

/// Default internal message queue depth for all dmq::os::Thread ports.
#define DMQ_DEFAULT_QUEUE_SIZE          20

/// Max number of threads that can be registered with the watchdog.
#define DMQ_MAX_WATCHDOG_THREADS        16

/// Duplicate-detection ring buffer depth per remote Participant.
/// Larger values catch more out-of-order duplicates; reduce on RAM-constrained targets.
#define DMQ_SEQ_HISTORY_SIZE            8

/// Max number of remote Participants the DataBus can hold without heap allocation.
#define DMQ_MAX_PARTICIPANTS            8

/// Default max remote peers per NetworkNode instance (fixed allocation).
/// Override per-instantiation via NetworkNode's MaxPeers template parameter.
#define DMQ_NETWORK_NODE_MAX_PEERS      4

/// Default max in- or out-topics per NetworkNode instance (fixed allocation).
/// Override per-instantiation via NetworkNode's MaxTopics template parameter.
#define DMQ_NETWORK_NODE_MAX_TOPICS     16

/// Max messages drained per NetworkNode::ReceiverThread() tick (incoming-message
/// drain and per-peer ACK drain).
#define DMQ_NETWORK_NODE_MAX_WORK       20

/// Default number of retries before RetryMonitor gives up on an unacknowledged message.
#define DMQ_RETRY_MONITOR_MAX_RETRIES   3

/// Default per-message ACK timeout (seconds) for TransportMonitor.
#define DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC 2

#endif // _DELEGATEMQ_CONFIG_H
