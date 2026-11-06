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

/// Stack size of each dmq::os::Thread on the RTOS ports, in each port's native
/// unit. FreeRTOS counts words and applies to dynamically allocated stacks
/// (FreeRTOSThread::SetStackMem() supplies a static stack instead); the others
/// count bytes.
#define DMQ_FREERTOS_STACK_WORDS        4096
#define DMQ_THREADX_STACK_BYTES         2048
#define DMQ_ZEPHYR_STACK_BYTES          2048
#define DMQ_CMSIS_RTOS2_STACK_BYTES     2048
#define DMQ_NUTTX_STACK_BYTES           8192

/// Default dmq::os::Thread priority on the RTOS ports whose default is a plain
/// number. SetThreadPriority() overrides it per thread. ThreadX: 0 is highest.
/// Zephyr: passed to K_PRIO_PREEMPT(). NuttX: SCHED_FIFO priority. (FreeRTOS and
/// CMSIS-RTOS2 derive their defaults from configMAX_PRIORITIES / osPriorityNormal.)
#define DMQ_THREADX_THREAD_PRIORITY     10
#define DMQ_ZEPHYR_THREAD_PRIORITY      5
#define DMQ_NUTTX_THREAD_PRIORITY       100

/// Fallback queue size (maxQueueSize == 0) for the desktop stdlib/Win32 Thread
/// ports only. A high-water-mark safety net (not a throughput limiter) against
/// unbounded growth if the destination thread is dead/stuck -- large enough to
/// never interfere with normal desktop bursts.
#define DMQ_THREAD_DESKTOP_QUEUE_SIZE   1000

/// Max number of threads that can be registered with the watchdog.
#define DMQ_MAX_WATCHDOG_THREADS        16

/// A watched thread's queue wait (and Qt's heartbeat timer) is its watchdog
/// timeout divided by this: an idle thread refreshes its heartbeat this many
/// times per timeout. Higher is more margin, more wakeups. At least 2.
#define DMQ_WATCHDOG_HEARTBEAT_DIVISOR  4

/// Max threads registered for dmq::ThisThread::GetCurrent() at once on RTOS ports
/// (one per running dmq::os::Thread or custom IThread worker). Exceeding it faults.
/// Unused on desktop ports, which use thread_local storage.
#define DMQ_MAX_CURRENT_THREADS         16

/// Duplicate-detection ring buffer depth per remote Participant.
/// Larger values catch more out-of-order duplicates; reduce on RAM-constrained targets.
#define DMQ_SEQ_HISTORY_SIZE            8

/// How long (ms) a received sequence number counts toward duplicate detection.
/// Should cover the sender's whole retry window: (retries + 1) x ACK timeout.
/// Older entries are ignored so a restarted peer's reused numbers aren't dropped.
#define DMQ_SEQ_HISTORY_EXPIRY_MS       ((DMQ_RETRY_MONITOR_MAX_RETRIES + 1) * DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC * 1000)

/// This node's ID, sent in every message header (1-255). Give every node on a
/// network a unique ID when more than one node publishes the same topic.
/// 0 = unset (duplicates tracked by remote ID alone). Run-time override:
/// dmq::transport::DmqHeader::SetLocalNodeId().
#define DMQ_NODE_ID                     0

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

/// Embedded UDP transports (lwIP, netconn, Zephyr, NetX): datagram buffer size
/// (header + payload, keep within the link MTU), the PUB socket's ACK-poll
/// receive timeout, and the SUB socket's receive timeout (how often its thread
/// can check for exit).
#define DMQ_UDP_BUFFER_SIZE             1500
#define DMQ_UDP_PUB_RECV_TIMEOUT_MS     50
#define DMQ_UDP_SUB_RECV_TIMEOUT_MS     2000

/// STM32 UART transport: RX ring buffer bytes (absorbs bursts between reads),
/// max payload per frame (both ends must agree), blocking transmit timeout per
/// frame (raise for low baud rates or large payloads), and the most Receive()
/// waits for the rest of a frame once its marker has arrived.
#define DMQ_UART_RX_RING_SIZE           1024
#define DMQ_UART_MAX_PAYLOAD            512
#define DMQ_UART_TX_TIMEOUT_MS          500
#define DMQ_UART_FRAME_TIMEOUT_MS       100

/// serialize (msg_serialize.h, DMQ_SERIALIZE_SERIALIZE): max string length and max
/// container element count, on write and on read. Both must be <= 65535 (16-bit
/// length field on the wire). Raising them also raises how much a received message
/// can make the receiver allocate.
#define DMQ_SERIALIZE_MAX_STRING_SIZE   1024
#define DMQ_SERIALIZE_MAX_CONTAINER_SIZE 64

/// serialize: max depth of serialize::I objects nested inside each other.
#define DMQ_SERIALIZE_MAX_NESTING       5

#endif // _DELEGATEMQ_CONFIG_H
