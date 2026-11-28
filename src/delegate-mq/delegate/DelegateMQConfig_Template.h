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

/// dmq::util::IsrDelegate: default number of posts it holds until
/// IsrDelegates::ProcessAll() takes them; a post to a full one is dropped.
#define DMQ_ISR_DELEGATE_QUEUE_SIZE     8

/// Signal Small-Buffer Optimization count.
/// Signals with <= this many subscribers are invoked without heap allocation.
#define DMQ_SIGNAL_SBO_COUNT            8

/// Default internal message queue depth (maxQueueSize == 0) for the RTOS
/// dmq::os::Thread ports (FreeRTOS, Zephyr, ThreadX, CMSIS-RTOS2, NuttX), where
/// the backing queue primitive requires a fixed capacity at creation.
#define DMQ_DEFAULT_QUEUE_SIZE          20

/// Stack size of each dmq::os::Thread on the RTOS ports, in each port's native
/// unit. FreeRTOS counts StackType_t units (words on mainline FreeRTOS, but bytes
/// on ESP-IDF, where StackType_t is uint8_t: 4096 is 16 KB on an STM32 and 4 KB on
/// an ESP32) and applies to dynamically allocated stacks
/// (FreeRTOSThread::SetStackMem() supplies a static stack instead); the others
/// count bytes.
#define DMQ_FREERTOS_STACK_WORDS        4096
#define DMQ_THREADX_STACK_BYTES         2048
#define DMQ_ZEPHYR_STACK_BYTES          2048
#define DMQ_CMSIS_RTOS2_STACK_BYTES     2048
#define DMQ_NUTTX_STACK_BYTES           8192

/// dmq::HighResClock on FreeRTOS and ThreadX on ARM Cortex-M adds SysTick's
/// progress through the current tick to the tick count, assuming SysTick is the
/// kernel tick (the default). If the kernel ticks from another timer, define
/// this: HighResClock then has tick resolution. See SysTickInterpolation.h.
// #define DMQ_NO_SYSTICK_INTERPOLATION

/// Default dmq::os::Thread priority on the RTOS ports whose default is a plain
/// number. SetThreadPriority() overrides it per thread. ThreadX: 0 is highest.
/// Zephyr: passed to K_PRIO_PREEMPT(). NuttX: SCHED_FIFO priority. (FreeRTOS and
/// CMSIS-RTOS2 derive their defaults from configMAX_PRIORITIES / osPriorityNormal.)
#define DMQ_THREADX_THREAD_PRIORITY     10
#define DMQ_ZEPHYR_THREAD_PRIORITY      5
#define DMQ_NUTTX_THREAD_PRIORITY       100

/// Fallback queue size (maxQueueSize == 0) for the desktop stdlib/Win32/POSIX/Qt
/// Thread ports only. A high-water-mark safety net (not a throughput limiter) against
/// unbounded growth if the destination thread is dead/stuck -- large enough to
/// never interfere with normal desktop bursts.
#define DMQ_THREAD_DESKTOP_QUEUE_SIZE   1000

/// DMQ_ALLOCATOR: static pools instead of heap blocks. Every block comes from
/// static memory and the heap is never used; a full pool faults. Set the
/// number of blocks of each size (header included, a power of two; 0 = none).
/// Get the numbers from xalloc_print_pool_config() after running the
/// application under its heaviest load, and add headroom.
// #define DMQ_XALLOCATOR_STATIC_POOLS
// #define DMQ_XALLOC_POOL_32     16
// #define DMQ_XALLOC_POOL_64     8
// #define DMQ_XALLOC_POOL_128    4

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

/// Default SpyReporter send queue: DataBus messages waiting to go to dmq-spy.
/// When it's full, new ones are dropped (counted, reported by
/// SpyReporter::OnDropped). Override per instance via SpyReporter's constructor.
#define DMQ_SPY_REPORTER_QUEUE_SIZE       64

/// Default NodeReporter send queue: heartbeat ticks and thread statistics
/// waiting to go to dmq-monitor / dmq-thread. When it's full, new ones are
/// dropped (counted, reported by NodeReporter::OnDropped). Override per
/// instance via NodeReporter's constructor.
#define DMQ_NODE_REPORTER_QUEUE_SIZE      16

/// Default NodeReporter heartbeat period (ms): how often a node sends its
/// heartbeat and health counters to dmq-monitor. Override per instance via
/// NodeReporter::Start().
#define DMQ_NODE_REPORTER_HEARTBEAT_MS    1000

/// Max messages drained per NetworkNode::ReceiverThread() tick (incoming-message
/// drain and per-peer ACK drain).
#define DMQ_NETWORK_NODE_MAX_WORK       20

/// Default number of retries before RetryMonitor gives up on an unacknowledged message.
#define DMQ_RETRY_MONITOR_MAX_RETRIES   3

/// Default per-message ACK timeout (seconds) for TransportMonitor.
#define DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC 2

/// Largest serialized message (payload bytes) a remote delegate can send; a
/// larger one fails with ERR_MESSAGE_TOO_LARGE before anything is allocated.
/// At most 65535. Default 65535, or 4096 under DMQ_ALLOCATOR (at most 8181
/// there: the buffer's block must fit the allocator's largest, 16 KB).
#define DMQ_MAX_MESSAGE_SIZE            65535

/// UDP transports: datagram buffer size (header + payload, keep within the link
/// MTU; embedded transports: lwIP, netconn, Zephyr, NetX), the PUB socket's
/// ACK-poll receive timeout and the SUB socket's receive timeout (how often its
/// thread can check for exit), for every UDP transport. Keep the PUB timeout
/// short: NetworkNode drains each peer's ACKs in turn and waits it out per peer.
#define DMQ_UDP_BUFFER_SIZE             1500
#define DMQ_UDP_PUB_RECV_TIMEOUT_MS     2
#define DMQ_UDP_SUB_RECV_TIMEOUT_MS     2000

/// Framed stream transports (FramedStreamTransport: Stm32UartTransport,
/// SerialTransport, any UART/USB-CDC link): RX ring buffer bytes (absorbs
/// bursts between reads), max payload per frame (both ends must agree),
/// blocking transmit timeout per frame (raise for low baud rates or large
/// payloads), the most Receive() waits for the rest of a frame once its marker
/// has arrived, and how long Receive() waits for a frame to start before
/// returning RECEIVE_NO_DATA (so its thread can check for exit).
#define DMQ_UART_RX_RING_SIZE           1024
#define DMQ_UART_MAX_PAYLOAD            512
#define DMQ_UART_TX_TIMEOUT_MS          500
#define DMQ_UART_FRAME_TIMEOUT_MS       100
#define DMQ_UART_RECV_TIMEOUT_MS        100

/// Shared-memory ring transport (ShmRingTransport, two cores or two processes):
/// slots per direction (a power of 2; a full ring fails Send()), max payload
/// bytes per message (both sides must agree: the region's layout depends on it),
/// and the most Receive() blocks for data when a signal (IShmSignal) is
/// attached. Footprint: about 2 x SLOT_COUNT x (MAX_PAYLOAD + 10) bytes,
/// plus a few hundred bytes of indices.
#define DMQ_SHM_SLOT_COUNT              16
#define DMQ_SHM_MAX_PAYLOAD             256
#define DMQ_SHM_RECV_TIMEOUT_MS         100

/// serialize (msg_serialize.h, DMQ_SERIALIZE_SERIALIZE): max string length and max
/// container element count, on write and on read. Both must be <= 65535 (16-bit
/// length field on the wire). Raising them also raises how much a received message
/// can make the receiver allocate.
#define DMQ_SERIALIZE_MAX_STRING_SIZE   1024
#define DMQ_SERIALIZE_MAX_CONTAINER_SIZE 64

/// serialize: max depth of serialize::I objects nested inside each other.
#define DMQ_SERIALIZE_MAX_NESTING       5

#endif // _DELEGATEMQ_CONFIG_H
