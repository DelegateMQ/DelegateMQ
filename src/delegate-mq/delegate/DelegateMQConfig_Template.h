#ifndef _DELEGATEMQ_CONFIG_H
#define _DELEGATEMQ_CONFIG_H

/// @file
/// @brief User configuration for the DelegateMQ library.
///
/// Copy this file to your project as DelegateMQConfig.h. It is picked up
/// automatically when its directory is on the include path, or point the
/// compiler at it explicitly: -DDMQ_USER_CONFIG="DelegateMQConfig.h"
///
/// Only define the values you want to change; any omitted values
/// fall back to the defaults in DelegateMQConfig_Default.h.

// --- Port and option selection (builds without CMake) ---
// Without CMake, choose ports and options here instead of with -D flags.
// Anything left undefined is auto-selected (see delegate/DelegateSelect.h):
// stdlib threads, the built-in serializer and UDP on Windows/Linux, nothing
// on bare metal. With CMake, set DMQ_THREAD etc. in CMake instead and leave
// these commented out; defining a second port here is a compile error.
//
// #define DMQ_THREAD_FREERTOS          // or DMQ_THREAD_STDLIB, _WIN32, _POSIX, _THREADX,
//                                      //    _ZEPHYR, _CMSIS_RTOS2, _NUTTX, _QT, _NONE
// #define DMQ_SERIALIZE_NONE           // or DMQ_SERIALIZE_SERIALIZE, _MSGPACK, ...
// #define DMQ_TRANSPORT_NONE           // or DMQ_TRANSPORT_STM32_UART, _ARM_LWIP_UDP, ...
// #define DMQ_ALLOCATOR                // fixed-block allocator instead of the heap
// #define DMQ_ASSERTS                  // FaultHandler() instead of exceptions
// #define DMQ_DATABUS                  // DataBus on an RTOS (desktop default is on;
// #define DMQ_DATABUS_OFF              //    DMQ_DATABUS_OFF turns it off there)
// #define DMQ_CUSTOM_FAULT_HANDLER     // application defines FaultHandler()/WatchdogHandler()
//                                      //    itself; port/fault/Fault.cpp compiles to nothing

/// Timeout (seconds) used by the TIMEOUT queue-full policy on all threads.
#define DMQ_DEFAULT_DISPATCH_TIMEOUT    2

/// Default quiet time (milliseconds) before a Thread's idle handler runs, and
/// between calls while the queue stays empty. See Thread::SetIdleHandler().
#define DMQ_THREAD_IDLE_INTERVAL        100

/// Max timers processed in one tick without heap allocation.
#define DMQ_MAX_TIMER_EXPIRED           16

/// Signal Small-Buffer Optimization count.
/// Signals with <= this many subscribers are invoked without heap allocation.
#define DMQ_SIGNAL_SBO_COUNT            8

/// Default internal message queue depth (maxQueueSize == 0) for the RTOS
/// dmq::os::Thread ports (FreeRTOS, Zephyr, ThreadX, CMSIS-RTOS2, NuttX), where
/// the backing queue primitive requires a fixed capacity at creation.
#define DMQ_DEFAULT_QUEUE_SIZE          20

/// Fallback queue size (maxQueueSize == 0) for the desktop stdlib/Win32 Thread
/// ports only. A high-water-mark safety net (not a throughput limiter) against
/// unbounded growth if the destination thread is dead/stuck -- large enough to
/// never interfere with normal desktop bursts.
#define DMQ_THREAD_DESKTOP_QUEUE_SIZE   1000

/// Max number of threads that can be registered with the watchdog.
#define DMQ_MAX_WATCHDOG_THREADS        16

/// Max threads registered for dmq::ThisThread::GetCurrent() at once on RTOS ports
/// (one per running dmq::os::Thread or custom IThread worker). Exceeding it faults.
/// Unused on desktop ports, which use thread_local storage.
#define DMQ_MAX_CURRENT_THREADS         16

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
