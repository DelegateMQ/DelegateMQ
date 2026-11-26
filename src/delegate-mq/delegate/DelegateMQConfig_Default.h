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

#ifndef DMQ_THREAD_IDLE_INTERVAL
    #define DMQ_THREAD_IDLE_INTERVAL        100     // milliseconds; default for Thread::SetIdleHandler()
#endif

#ifndef DMQ_MAX_TIMER_EXPIRED
    #define DMQ_MAX_TIMER_EXPIRED           16
#endif

// dmq::util::IsrDelegate: posts it holds until IsrDelegates::ProcessAll()
#ifndef DMQ_ISR_DELEGATE_QUEUE_SIZE
    #define DMQ_ISR_DELEGATE_QUEUE_SIZE     8
#endif

#ifndef DMQ_SIGNAL_SBO_COUNT
    #define DMQ_SIGNAL_SBO_COUNT            8
#endif

#ifndef DMQ_DEFAULT_QUEUE_SIZE
    #define DMQ_DEFAULT_QUEUE_SIZE          20
#endif

// Stack size of each dmq::os::Thread on the RTOS ports, in each port's native unit.
#ifndef DMQ_FREERTOS_STACK_WORDS
    #define DMQ_FREERTOS_STACK_WORDS        4096    // StackType_t units: words, but bytes on ESP-IDF; dynamic stacks (SetStackMem() overrides)
#endif
#ifndef DMQ_THREADX_STACK_BYTES
    #define DMQ_THREADX_STACK_BYTES         2048
#endif
#ifndef DMQ_ZEPHYR_STACK_BYTES
    #define DMQ_ZEPHYR_STACK_BYTES          2048
#endif
#ifndef DMQ_CMSIS_RTOS2_STACK_BYTES
    #define DMQ_CMSIS_RTOS2_STACK_BYTES     2048
#endif
#ifndef DMQ_NUTTX_STACK_BYTES
    #define DMQ_NUTTX_STACK_BYTES           8192
#endif

// Default dmq::os::Thread priority on the RTOS ports whose default is a plain number
// (SetThreadPriority() overrides per thread).
#ifndef DMQ_THREADX_THREAD_PRIORITY
    #define DMQ_THREADX_THREAD_PRIORITY     10      // 0 = highest
#endif
#ifndef DMQ_ZEPHYR_THREAD_PRIORITY
    #define DMQ_ZEPHYR_THREAD_PRIORITY      5       // passed to K_PRIO_PREEMPT()
#endif
#ifndef DMQ_NUTTX_THREAD_PRIORITY
    #define DMQ_NUTTX_THREAD_PRIORITY       100     // SCHED_FIFO
#endif

// Fallback queue size (maxQueueSize == 0) for the desktop stdlib/Win32/POSIX/Qt Thread ports
// only. Unlike DMQ_DEFAULT_QUEUE_SIZE (sized for real RTOS RAM constraints), these
// ports back their queue with a plain std::deque, so maxQueueSize == 0 otherwise
// means genuinely unbounded growth if the destination thread is dead/stuck. This
// is a high-water-mark safety net, not a throughput limiter -- large enough to
// never interfere with normal desktop bursts, but bounded so a truly dead consumer
// hits a clear FullPolicy::FAULT instead of growing the heap without limit.
#ifndef DMQ_THREAD_DESKTOP_QUEUE_SIZE
    #define DMQ_THREAD_DESKTOP_QUEUE_SIZE   1000
#endif

#ifndef DMQ_MAX_WATCHDOG_THREADS
    #define DMQ_MAX_WATCHDOG_THREADS        16
#endif

// A watched thread's queue wait (and Qt's heartbeat timer) is its watchdog timeout
// divided by this, so an idle thread refreshes its heartbeat this many times per
// timeout. At least 2.
#ifndef DMQ_WATCHDOG_HEARTBEAT_DIVISOR
    #define DMQ_WATCHDOG_HEARTBEAT_DIVISOR  4
#endif

// Max threads registered for dmq::ThisThread::GetCurrent() at once on RTOS ports
// (one per running dmq::os::Thread or custom IThread worker). Exceeding it faults.
#ifndef DMQ_MAX_CURRENT_THREADS
    #define DMQ_MAX_CURRENT_THREADS         16
#endif

#ifndef DMQ_SEQ_HISTORY_SIZE
    #define DMQ_SEQ_HISTORY_SIZE            8
#endif

// How long a received sequence number counts toward duplicate detection. A real
// duplicate (a retry after a lost ACK) arrives within the sender's retry window;
// older entries are ignored so a restarted peer's reused numbers aren't dropped.
#ifndef DMQ_SEQ_HISTORY_EXPIRY_MS
    #define DMQ_SEQ_HISTORY_EXPIRY_MS       ((DMQ_RETRY_MONITOR_MAX_RETRIES + 1) * DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC * 1000)
#endif

// This node's ID, sent in every DmqHeader (1-255). 0 = unset: receivers then
// track duplicates by remote ID alone, which is only correct with one sender
// per topic. Can be changed at run time with DmqHeader::SetLocalNodeId().
#ifndef DMQ_NODE_ID
    #define DMQ_NODE_ID                     0
#endif

#ifndef DMQ_MAX_PARTICIPANTS
    #define DMQ_MAX_PARTICIPANTS            8
#endif

#ifndef DMQ_TRANSPORT_MONITOR_MAX_PENDING
    #define DMQ_TRANSPORT_MONITOR_MAX_PENDING 100
#endif

// Largest serialized message (payload bytes, no header) a remote delegate can
// send: a ByteWriter fails (ERR_MESSAGE_TOO_LARGE) instead of growing past it.
// At most 65535 (the header's 16-bit length). Under DMQ_ALLOCATOR the buffer's
// block must fit the fixed-block allocator's largest (16 KB), so the default is
// smaller there.
#ifndef DMQ_MAX_MESSAGE_SIZE
    #ifdef DMQ_ALLOCATOR
        #define DMQ_MAX_MESSAGE_SIZE        4096
    #else
        #define DMQ_MAX_MESSAGE_SIZE        65535
    #endif
#endif

// UDP transports: datagram buffer size (header + payload; the embedded ones:
// lwIP, netconn, Zephyr, NetX) and receive timeouts (every UDP transport,
// desktop too). A PUB socket polls briefly for ACKs, so a node that drains
// several peers' ACKs in turn (NetworkNode) isn't held up; a SUB socket waits
// longer, returning RECEIVE_NO_DATA so its thread can check for exit. A thread
// that polls a SUB socket between other work needs SetRecvTimeout() shorter.
#ifndef DMQ_UDP_BUFFER_SIZE
    #define DMQ_UDP_BUFFER_SIZE             1500
#endif
#ifndef DMQ_UDP_PUB_RECV_TIMEOUT_MS
    #define DMQ_UDP_PUB_RECV_TIMEOUT_MS     2
#endif
#ifndef DMQ_UDP_SUB_RECV_TIMEOUT_MS
    #define DMQ_UDP_SUB_RECV_TIMEOUT_MS     2000
#endif

// STM32 UART transport: RX ring buffer bytes, max payload per frame, blocking
// transmit timeout per frame, and the most it waits for the rest of a frame
// once its marker has arrived.
#ifndef DMQ_UART_RX_RING_SIZE
    #define DMQ_UART_RX_RING_SIZE           1024
#endif
#ifndef DMQ_UART_MAX_PAYLOAD
    #define DMQ_UART_MAX_PAYLOAD            512
#endif
#ifndef DMQ_UART_TX_TIMEOUT_MS
    #define DMQ_UART_TX_TIMEOUT_MS          500
#endif
#ifndef DMQ_UART_FRAME_TIMEOUT_MS
    #define DMQ_UART_FRAME_TIMEOUT_MS       100
#endif

// Shared-memory ring transport (ShmRingTransport): slots per direction (a
// power of 2), max payload bytes per message (both sides must agree), and the
// most Receive() waits for data when an IShmSignal is set.
#ifndef DMQ_SHM_SLOT_COUNT
    #define DMQ_SHM_SLOT_COUNT              16
#endif
#ifndef DMQ_SHM_MAX_PAYLOAD
    #define DMQ_SHM_MAX_PAYLOAD             256
#endif
#ifndef DMQ_SHM_RECV_TIMEOUT_MS
    #define DMQ_SHM_RECV_TIMEOUT_MS         100
#endif

// Maximum number of distinct fixed-block allocator buckets (heap-blocks mode only).
// Each unique power-of-two allocation size gets one bucket. Most applications use
// 8-12 distinct sizes; raise this only if xallocator's ASSERT fires at runtime.
#ifndef DMQ_XALLOCATOR_MAX_ALLOCATORS
    #define DMQ_XALLOCATOR_MAX_ALLOCATORS   15
#endif

// Static pools (DMQ_ALLOCATOR with DMQ_XALLOCATOR_STATIC_POOLS defined): blocks of
// each size (header included, a power of two) in static memory; the heap is never
// used. 0: no pool for that size, and an allocation of it faults. Size them with
// xalloc_print_pool_config() after running under the heaviest load.
#ifndef DMQ_XALLOC_POOL_16
    #define DMQ_XALLOC_POOL_16     0
#endif
#ifndef DMQ_XALLOC_POOL_32
    #define DMQ_XALLOC_POOL_32     0
#endif
#ifndef DMQ_XALLOC_POOL_64
    #define DMQ_XALLOC_POOL_64     0
#endif
#ifndef DMQ_XALLOC_POOL_128
    #define DMQ_XALLOC_POOL_128    0
#endif
#ifndef DMQ_XALLOC_POOL_256
    #define DMQ_XALLOC_POOL_256    0
#endif
#ifndef DMQ_XALLOC_POOL_512
    #define DMQ_XALLOC_POOL_512    0
#endif
#ifndef DMQ_XALLOC_POOL_1024
    #define DMQ_XALLOC_POOL_1024   0
#endif
#ifndef DMQ_XALLOC_POOL_2048
    #define DMQ_XALLOC_POOL_2048   0
#endif
#ifndef DMQ_XALLOC_POOL_4096
    #define DMQ_XALLOC_POOL_4096   0
#endif
#ifndef DMQ_XALLOC_POOL_8192
    #define DMQ_XALLOC_POOL_8192   0
#endif
#ifndef DMQ_XALLOC_POOL_16384
    #define DMQ_XALLOC_POOL_16384  0
#endif

// Default max remote peers per NetworkNode instance (fixed allocation).
// Override per-instantiation via NetworkNode's MaxPeers template parameter.
#ifndef DMQ_NETWORK_NODE_MAX_PEERS
    #define DMQ_NETWORK_NODE_MAX_PEERS      4
#endif

// Default max in- or out-topics per NetworkNode instance (fixed allocation).
// Override per-instantiation via NetworkNode's MaxTopics template parameter.
#ifndef DMQ_NETWORK_NODE_MAX_TOPICS
    #define DMQ_NETWORK_NODE_MAX_TOPICS     16
#endif

// Default SpyReporter send queue: DataBus messages waiting to go to dmq-spy.
// When it's full, new ones are dropped (counted, reported by
// SpyReporter::OnDropped). Override per instance via SpyReporter's constructor.
#ifndef DMQ_SPY_REPORTER_QUEUE_SIZE
    #define DMQ_SPY_REPORTER_QUEUE_SIZE       64
#endif

// Default NodeReporter send queue: heartbeat ticks and thread statistics
// waiting to go to dmq-monitor / dmq-thread. When it's full, new ones are
// dropped (counted, reported by NodeReporter::OnDropped). Override per
// instance via NodeReporter's constructor.
#ifndef DMQ_NODE_REPORTER_QUEUE_SIZE
    #define DMQ_NODE_REPORTER_QUEUE_SIZE      16
#endif

// Default NodeReporter heartbeat period (ms): how often a node sends its
// heartbeat and health counters to dmq-monitor. Override per instance via
// NodeReporter::Start().
#ifndef DMQ_NODE_REPORTER_HEARTBEAT_MS
    #define DMQ_NODE_REPORTER_HEARTBEAT_MS    1000
#endif

// Max messages drained per NetworkNode::ReceiverThread() tick (applies to both
// the incoming-message drain and the per-peer ACK drain).
#ifndef DMQ_NETWORK_NODE_MAX_WORK
    #define DMQ_NETWORK_NODE_MAX_WORK       20
#endif

// Default number of retries before RetryMonitor gives up on an unacknowledged message.
#ifndef DMQ_RETRY_MONITOR_MAX_RETRIES
    #define DMQ_RETRY_MONITOR_MAX_RETRIES   3
#endif

// Default per-message ACK timeout (seconds) for TransportMonitor.
#ifndef DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC
    #define DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC 2
#endif

// serialize (msg_serialize.h, DMQ_SERIALIZE_SERIALIZE) limits. Longer strings or
// larger containers fail with STRING_TOO_LONG / CONTAINER_TOO_MANY, on write and
// on read. Both must be <= 65535 (16-bit length field on the wire).
#ifndef DMQ_SERIALIZE_MAX_STRING_SIZE
    #define DMQ_SERIALIZE_MAX_STRING_SIZE   1024
#endif
#ifndef DMQ_SERIALIZE_MAX_CONTAINER_SIZE
    #define DMQ_SERIALIZE_MAX_CONTAINER_SIZE 64
#endif
// Max depth of serialize::I objects nested inside each other (NESTING_TOO_DEEP beyond).
#ifndef DMQ_SERIALIZE_MAX_NESTING
    #define DMQ_SERIALIZE_MAX_NESTING       5
#endif

#endif // _DELEGATEMQ_CONFIG_DEFAULT_H
