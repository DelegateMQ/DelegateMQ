#ifndef _DELEGATE_OPT_H
#define _DELEGATE_OPT_H

/// @file
/// @brief Delegate library options header file.

// Option selection (user config, port auto-detection, derived DMQ_* macros)
// comes first so everything below sees the final options.
#include "DelegateSelect.h"

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    // If networking is active, include winsock2.h here to prevent macro 
    // redefinitions when windows.h is included later (e.g. by FreeRTOS or stdlib).
    #if defined(DMQ_DATABUS) || defined(DMQ_TRANSPORT_ZEROMQ) || \
        defined(DMQ_TRANSPORT_WIN32_UDP) || defined(DMQ_TRANSPORT_WIN32_TCP) || \
        defined(DMQ_TRANSPORT_WIN32_PIPE) || defined(DMQ_TRANSPORT_NNG) || \
        defined(DMQ_TRANSPORT_MQTT)
        #include <winsock2.h>
        #include <ws2tcpip.h>
    #endif
#endif

// Host BSD/Winsock socket headers for extras/util/NetworkConnect.h's desktop
// convenience helpers (RAII WSAStartup/WSACleanup, GetLocalAddress()).
//
// _WIN32/__linux__/__APPLE__/__unix__ reflect the compiler/host doing the
// building, not the actual target. A genuine embedded cross-compile (e.g.
// arm-none-eabi-gcc for stm32-freertos) never defines these host-platform
// macros in the first place, so this guard is only ever live for a target
// that is ALSO built with a host compiler -- i.e. a simulator/native sample.
// DMQ_THREAD_HAS_NATIVE_NETWORK_STACK (above) captures exactly which
// embedded RTOS ports bundle a colliding native network stack there and
// must be excluded; see its own comment for why FreeRTOS is deliberately
// not one of them.
#if !defined(DMQ_THREAD_HAS_NATIVE_NETWORK_STACK)
    #define DMQ_NETWORK_CONNECT_DESKTOP_HOST_HEADERS
    #ifdef _WIN32
        #include <winsock2.h>
        #include <ws2tcpip.h>
        #pragma comment(lib, "ws2_32.lib")
    #elif defined(__linux__) || defined(__APPLE__) || defined(__unix__)
        #include <unistd.h>
        #include <sys/types.h>
        #include <sys/socket.h>
        #include <netinet/in.h>
        #include <arpa/inet.h>
        #include <netdb.h>
        #include <ifaddrs.h>
        #include <cstring>
        #include <net/if.h>
    #endif
#endif

#include <chrono>
#if defined(DMQ_HAS_THREADING)
    #include <mutex>
    #if !defined(__cpp_lib_scoped_lock)
        // No std::scoped_lock on this toolchain; see dmq::ScopedLock below.
        #include "port/os/common/PortableScopedLock.h"
    #endif
#endif

// RTTI Detection Check
#if !defined(__cpp_rtti) && !defined(__GXX_RTTI) && !defined(_CPPRTTI)
    #error "RTTI compiler option is disabled but required by the DelegateMQ library."
#endif

// Some OS port headers below (e.g. ThreadXMutex.h, ThreadXConditionVariable.h) use
// DMQ_ASSERT_TRUE in constructors. Include Fault.h here, ahead of those port headers,
// so the macro is defined before first use. Fault.h is include-guarded, so the
// later #include "extras/util/Fault.h" below is a harmless no-op.
#include "extras/util/Fault.h"

#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_QT) || defined(DMQ_THREAD_POSIX)
    // Windows / Linux / macOS / Qt (Standard Library) / POSIX (raw pthreads,
    // but Mutex/ConditionVariable/Clock/ThisThread still reuse std:: here --
    // see port/os/posix/PosixThread.h, the only file this port adds)
    #include <condition_variable>
    #include <thread>
    #include "port/os/stdlib/StdlibThisThread.h"
#elif defined(DMQ_THREAD_FREERTOS)
    #include "port/os/freertos/FreeRTOSClock.h"
    #include "port/os/freertos/FreeRTOSMutex.h"
    #include "port/os/freertos/FreeRTOSConditionVariable.h"
    #include "port/os/freertos/FreeRTOSCriticalSection.h"
    #include "port/os/freertos/FreeRTOSThisThread.h"
#elif defined(DMQ_THREAD_THREADX)
    #include "port/os/threadx/ThreadXClock.h"
    #include "port/os/threadx/ThreadXMutex.h"
    #include "port/os/threadx/ThreadXConditionVariable.h"
    #include "port/os/threadx/ThreadXCriticalSection.h"
    #include "port/os/threadx/ThreadXThisThread.h"
#elif defined(DMQ_THREAD_ZEPHYR)
    #include "port/os/zephyr/ZephyrClock.h"
    #include "port/os/zephyr/ZephyrMutex.h"
    #include "port/os/zephyr/ZephyrCriticalSection.h"
    #include "port/os/zephyr/ZephyrSemaphore.h"
    #include "port/os/zephyr/ZephyrThisThread.h"
#elif defined(DMQ_THREAD_CMSIS_RTOS2)
    #include "port/os/cmsis-rtos2/CmsisRtos2Clock.h"
    #include "port/os/cmsis-rtos2/CmsisRtos2Mutex.h"
    #include "port/os/cmsis-rtos2/CmsisRtos2CriticalSection.h"
    #include "port/os/cmsis-rtos2/CmsisRtos2Semaphore.h"
    #include "port/os/cmsis-rtos2/CmsisRtos2ThisThread.h"
#elif defined(DMQ_THREAD_NUTTX)
    #include "port/os/nuttx/NuttXClock.h"
    #include "port/os/nuttx/NuttXMutex.h"
    #include "port/os/nuttx/NuttXCriticalSection.h"
    #include "port/os/nuttx/NuttXSemaphore.h"
    #include "port/os/nuttx/NuttXThisThread.h"
#elif defined(DMQ_THREAD_NONE)
    #include "port/os/bare-metal/BareMetalClock.h"
    #include "port/os/bare-metal/BareMetalCriticalSection.h"
    #include "port/os/bare-metal/BareMetalThisThread.h"
#else
    #include "port/os/bare-metal/BareMetalClock.h"
    #include "port/os/bare-metal/BareMetalCriticalSection.h"
    #include "port/os/bare-metal/BareMetalThisThread.h"
#endif

namespace dmq
{
    // --- PORTABLE LOCK GUARD ---
    // Does not require <mutex>. Works with any BasicLockable type (lock/unlock).
    template<typename T>
    class PortableLockGuard {
        T& m_mutex;
    public:
        explicit PortableLockGuard(T& m) noexcept : m_mutex(m) { m_mutex.lock(); }
        ~PortableLockGuard() noexcept { m_mutex.unlock(); }
        PortableLockGuard(const PortableLockGuard&) = delete;
        PortableLockGuard& operator=(const PortableLockGuard&) = delete;
    };

    // --- PORTABLE SCOPED LOCK ---
    // std::scoped_lock on threaded ports (desktop + RTOS); all RTOS mutex types
    // satisfy BasicLockable (lock/unlock), so it works with them directly. Some
    // bare-metal standard libraries lack std::scoped_lock without thread support
    // (e.g. Arm GNU Toolchain 14); those get dmq::os::PortableScopedLock
    // (port/os/common/PortableScopedLock.h). No-op for DMQ_THREAD_NONE and for
    // any bare-metal build with no thread model.
#if defined(DMQ_HAS_THREADING) && defined(__cpp_lib_scoped_lock)
    template <typename... M>
    using ScopedLock = std::scoped_lock<M...>;
#elif defined(DMQ_HAS_THREADING)
    template <typename... M>
    using ScopedLock = dmq::os::PortableScopedLock<M...>;
#else
    // No-op: DMQ_THREAD_NONE or no thread model defined (bare-metal, single-threaded)
    template <typename... M>
    class ScopedLock {
    public:
        explicit ScopedLock(M&...) noexcept {}
        ~ScopedLock() noexcept = default;
        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;
    };
#endif

    // @TODO: Change aliases to switch clock type globally if necessary

    // --- CLOCK SELECTION ---
#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_QT) || defined(DMQ_THREAD_POSIX)
    // Windows / Linux / macOS / Qt / POSIX
    using Clock = std::chrono::steady_clock;

#elif defined(DMQ_THREAD_FREERTOS)
    // Use the custom FreeRTOS wrapper
    using Clock = dmq::os::FreeRTOSClock;

#elif defined(DMQ_THREAD_THREADX)
    // Use the custom ThreadX wrapper
    using Clock = dmq::os::ThreadXClock;

#elif defined(DMQ_THREAD_ZEPHYR)
    // Use the custom Zephyr wrapper
    using Clock = dmq::os::ZephyrClock;

#elif defined(DMQ_THREAD_CMSIS_RTOS2)
    using Clock = dmq::os::CmsisRtos2Clock;

#elif defined(DMQ_THREAD_NUTTX)
    // Use the custom NuttX wrapper
    using Clock = dmq::os::NuttXClock;

#else
    // Assuming implemented the 'g_ticks' variable
    using Clock = dmq::os::BareMetalClock;
#endif

    // --- GENERIC TYPES ---
    // Automatically adapt to the underlying Clock's traits
    using Duration = typename Clock::duration;
    using TimePoint = typename Clock::time_point;

    // --- THIS_THREAD (sleep_for / yield / GetCurrent) SELECTION ---
    // Portable equivalents of std::this_thread::sleep_for()/yield() for the
    // calling thread, plus GetCurrent(): the dmq::IThread whose worker is the
    // calling thread, or nullptr (registered by dmq::CurrentThreadScope in
    // IThread.h). Exists so library internals that need to delay or yield
    // (e.g. RetryMonitor backoff) aren't forced to pull in a full
    // dmq::os::Thread -- which also drags in the message queue, watchdog, and
    // stats machinery -- just to sleep. Each dmq::os::Thread::Sleep() forwards
    // here too, so every port's native delay call is implemented exactly once.
    // Each port implements it in port/os/<os>/<Os>ThisThread.h.
    //
    // A class (not a "this_thread" namespace) deliberately: sample/app code
    // throughout this project does `using namespace dmq; ... using namespace
    // std;` together, and a same-named nested namespace would make unqualified
    // this_thread::sleep_for() calls in that code ambiguous against
    // std::this_thread.
#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_QT) || defined(DMQ_THREAD_POSIX)
    using ThisThread = dmq::os::StdlibThisThread;
#elif defined(DMQ_THREAD_FREERTOS)
    using ThisThread = dmq::os::FreeRTOSThisThread;
#elif defined(DMQ_THREAD_THREADX)
    using ThisThread = dmq::os::ThreadXThisThread;
#elif defined(DMQ_THREAD_ZEPHYR)
    using ThisThread = dmq::os::ZephyrThisThread;
#elif defined(DMQ_THREAD_CMSIS_RTOS2)
    using ThisThread = dmq::os::CmsisRtos2ThisThread;
#elif defined(DMQ_THREAD_NUTTX)
    using ThisThread = dmq::os::NuttXThisThread;
#else
    // Bare metal (DMQ_THREAD_NONE, or no thread model defined): sleep_for()
    // busy-waits against BareMetalClock; yield() is a no-op.
    using ThisThread = dmq::os::BareMetalThisThread;
#endif

    /// @brief Policy applied when a dmq::os::Thread port's message queue is full.
    /// @details Always enforced -- a constructor's maxQueueSize == 0 is a sentinel meaning
    /// "use this port's default capacity" (dmq::DEFAULT_QUEUE_SIZE for the RTOS ports,
    /// dmq::THREAD_DESKTOP_QUEUE_SIZE for stdlib/Win32), not "disable the cap." Every port's
    /// effective queue capacity is therefore always > 0 at runtime.
    ///   - DROP:    DispatchDelegate() silently discards the message and returns immediately.
    ///   - FAULT:   DispatchDelegate() triggers a system fault if the queue is full.
    ///   - TIMEOUT: DispatchDelegate() waits up to dispatchTimeout, then logs and drops.
    ///
    /// Use DROP for high-rate best-effort topics (sensor telemetry, display updates) where
    /// a stale sample is preferable to stalling the publisher. Use TIMEOUT for critical topics
    /// (commands, state transitions) where every message should be delivered if possible.
    /// FAULT is the default. Every dmq::os::Thread port aliases this single definition as
    /// dmq::os::FullPolicy so existing port-qualified references keep working.
    enum class FullPolicy { DROP, FAULT, TIMEOUT };

    /// @brief What a dmq::os::Thread does with messages still queued when ExitThread() is called.
    ///   - DRAIN:   Invoke every message queued before ExitThread(), then exit (default).
    ///   - DISCARD: Invoke only the message already running, then exit. Queued messages are
    ///              discarded without being invoked; a sender blocked on one (an async-wait
    ///              delegate) is released and sees the call fail (DelegateMsg::Cancel()).
    ///
    /// Use DISCARD for fast shutdown when pending work is disposable (telemetry, UI refresh).
    /// A thread that exits itself (ExitThread() called from its own message handler) always
    /// discards, since the thread's owner may already be destroyed.
    enum class ExitPolicy { DRAIN, DISCARD };

    /// @brief Default timeout for the TIMEOUT queue-full policy across all Thread ports.
    /// Override via DMQ_DEFAULT_DISPATCH_TIMEOUT in DelegateMQConfig.h.
    inline constexpr std::chrono::seconds DEFAULT_DISPATCH_TIMEOUT{DMQ_DEFAULT_DISPATCH_TIMEOUT};

    /// @brief Default quiet time before a Thread's idle handler runs, and between
    /// calls while its queue stays empty. See Thread::SetIdleHandler().
    /// Override via DMQ_THREAD_IDLE_INTERVAL in DelegateMQConfig.h.
    inline constexpr std::chrono::milliseconds THREAD_IDLE_INTERVAL{DMQ_THREAD_IDLE_INTERVAL};
    static_assert(DMQ_THREAD_IDLE_INTERVAL > 0, "DMQ_THREAD_IDLE_INTERVAL must be greater than zero");

    // --- RESOURCE LIMITS & SBO CONFIGURATION ---

    /// @brief Max timers processed in one tick without heap allocation.
    /// Override via DMQ_MAX_TIMER_EXPIRED in delegatemqconfig.h.
    inline constexpr size_t MAX_TIMER_EXPIRED = DMQ_MAX_TIMER_EXPIRED;

    /// @brief Signal Small-Buffer Optimization (SBO) count.
    /// Signals with <= this many subscribers are invoked heap-free.
    /// Override via DMQ_SIGNAL_SBO_COUNT in delegatemqconfig.h.
    inline constexpr size_t SIGNAL_SBO_COUNT = DMQ_SIGNAL_SBO_COUNT;

    /// @brief Default internal queue size (maxQueueSize == 0) for the RTOS
    /// dmq::os::Thread ports, where the backing queue primitive requires a
    /// fixed capacity at creation.
    /// Override via DMQ_DEFAULT_QUEUE_SIZE in delegatemqconfig.h.
    inline constexpr size_t DEFAULT_QUEUE_SIZE = DMQ_DEFAULT_QUEUE_SIZE;

    /// @brief Stack size of each dmq::os::Thread on the RTOS ports, in each
    /// port's native unit (FreeRTOS: words, for dynamically allocated stacks;
    /// the others: bytes).
    /// Override via DMQ_FREERTOS_STACK_WORDS, DMQ_THREADX_STACK_BYTES,
    /// DMQ_ZEPHYR_STACK_BYTES, DMQ_CMSIS_RTOS2_STACK_BYTES and
    /// DMQ_NUTTX_STACK_BYTES in delegatemqconfig.h.
    inline constexpr uint32_t FREERTOS_STACK_WORDS = DMQ_FREERTOS_STACK_WORDS;
    inline constexpr size_t THREADX_STACK_BYTES = DMQ_THREADX_STACK_BYTES;
    inline constexpr size_t ZEPHYR_STACK_BYTES = DMQ_ZEPHYR_STACK_BYTES;
    inline constexpr size_t CMSIS_RTOS2_STACK_BYTES = DMQ_CMSIS_RTOS2_STACK_BYTES;
    inline constexpr size_t NUTTX_STACK_BYTES = DMQ_NUTTX_STACK_BYTES;

    /// @brief Default dmq::os::Thread priority on the ThreadX, Zephyr (passed to
    /// K_PRIO_PREEMPT()) and NuttX (SCHED_FIFO) ports. SetThreadPriority()
    /// overrides it per thread.
    /// Override via DMQ_THREADX_THREAD_PRIORITY, DMQ_ZEPHYR_THREAD_PRIORITY and
    /// DMQ_NUTTX_THREAD_PRIORITY in delegatemqconfig.h.
    inline constexpr int THREADX_THREAD_PRIORITY = DMQ_THREADX_THREAD_PRIORITY;
    inline constexpr int ZEPHYR_THREAD_PRIORITY = DMQ_ZEPHYR_THREAD_PRIORITY;
    inline constexpr int NUTTX_THREAD_PRIORITY = DMQ_NUTTX_THREAD_PRIORITY;

    /// @brief Fallback queue size (maxQueueSize == 0) for the desktop
    /// stdlib/Win32 Thread ports only, which back their queue with a plain
    /// std::deque and would otherwise grow without bound if the destination
    /// thread is dead/stuck. A high-water-mark safety net, not a throughput
    /// limiter -- large enough to never interfere with normal desktop bursts.
    /// Override via DMQ_THREAD_DESKTOP_QUEUE_SIZE in delegatemqconfig.h.
    inline constexpr size_t THREAD_DESKTOP_QUEUE_SIZE = DMQ_THREAD_DESKTOP_QUEUE_SIZE;

    /// @brief Max number of threads that can be monitored by the watchdog.
    /// Override via DMQ_MAX_WATCHDOG_THREADS in delegatemqconfig.h.
    inline constexpr size_t MAX_WATCHDOG_THREADS = DMQ_MAX_WATCHDOG_THREADS;

    /// @brief A watched thread's queue wait (and Qt's heartbeat timer) is its
    /// watchdog timeout divided by this.
    /// Override via DMQ_WATCHDOG_HEARTBEAT_DIVISOR in delegatemqconfig.h.
    inline constexpr int WATCHDOG_HEARTBEAT_DIVISOR = DMQ_WATCHDOG_HEARTBEAT_DIVISOR;
    static_assert(WATCHDOG_HEARTBEAT_DIVISOR >= 2, "DMQ_WATCHDOG_HEARTBEAT_DIVISOR must be at least 2");

    /// @brief Max threads registered for ThisThread::GetCurrent() at once on RTOS
    /// ports (see port/os/common/CurrentThreadStorage.h). Exceeding it faults.
    /// Override via DMQ_MAX_CURRENT_THREADS in delegatemqconfig.h.
    inline constexpr size_t MAX_CURRENT_THREADS = DMQ_MAX_CURRENT_THREADS;

    /// @brief Max number of remote Participants the DataBus can hold without heap allocation.
    /// Override via DMQ_MAX_PARTICIPANTS in delegatemqconfig.h.
    inline constexpr size_t MAX_PARTICIPANTS = DMQ_MAX_PARTICIPANTS;

    /// @brief serialize (msg_serialize.h) limits: max string length, max container
    /// element count and max nesting depth of serialize::I objects.
    /// Override via DMQ_SERIALIZE_MAX_STRING_SIZE, DMQ_SERIALIZE_MAX_CONTAINER_SIZE
    /// and DMQ_SERIALIZE_MAX_NESTING in delegatemqconfig.h.
    inline constexpr uint16_t SERIALIZE_MAX_STRING_SIZE = DMQ_SERIALIZE_MAX_STRING_SIZE;
    inline constexpr uint16_t SERIALIZE_MAX_CONTAINER_SIZE = DMQ_SERIALIZE_MAX_CONTAINER_SIZE;
    inline constexpr size_t SERIALIZE_MAX_NESTING = DMQ_SERIALIZE_MAX_NESTING;
    static_assert(DMQ_SERIALIZE_MAX_STRING_SIZE <= 65535 && DMQ_SERIALIZE_MAX_CONTAINER_SIZE <= 65535,
        "serialize length fields are 16 bits");
    static_assert(DMQ_SERIALIZE_MAX_NESTING >= 1, "DMQ_SERIALIZE_MAX_NESTING must be at least 1");

    /// @brief Default max remote peers per NetworkNode instance (fixed allocation).
    /// Override via DMQ_NETWORK_NODE_MAX_PEERS in delegatemqconfig.h, or per-instantiation
    /// via NetworkNode's MaxPeers template parameter.
    inline constexpr size_t NETWORK_NODE_MAX_PEERS = DMQ_NETWORK_NODE_MAX_PEERS;

    /// @brief Default max in- or out-topics per NetworkNode instance (fixed allocation).
    /// Override via DMQ_NETWORK_NODE_MAX_TOPICS in delegatemqconfig.h, or per-instantiation
    /// via NetworkNode's MaxTopics template parameter.
    inline constexpr size_t NETWORK_NODE_MAX_TOPICS = DMQ_NETWORK_NODE_MAX_TOPICS;

    /// @brief Max messages drained per NetworkNode::ReceiverThread() tick.
    /// Override via DMQ_NETWORK_NODE_MAX_WORK in delegatemqconfig.h.
    inline constexpr int NETWORK_NODE_MAX_WORK = DMQ_NETWORK_NODE_MAX_WORK;

    /// @brief Default number of retries before RetryMonitor gives up on an unacknowledged message.
    /// Override via DMQ_RETRY_MONITOR_MAX_RETRIES in delegatemqconfig.h, or per-call via
    /// RetryMonitor's maxRetries constructor/Init() parameter.
    inline constexpr int RETRY_MONITOR_MAX_RETRIES = DMQ_RETRY_MONITOR_MAX_RETRIES;

    /// @brief Default per-message ACK timeout for TransportMonitor.
    /// Override via DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC in delegatemqconfig.h, or per-instance
    /// via TransportMonitor's timeout constructor parameter.
    inline constexpr Duration TRANSPORT_MONITOR_TIMEOUT = std::chrono::seconds(DMQ_TRANSPORT_MONITOR_TIMEOUT_SEC);

    /// @brief How long a received sequence number counts toward duplicate detection.
    /// Override via DMQ_SEQ_HISTORY_EXPIRY_MS in delegatemqconfig.h.
    inline constexpr Duration SEQ_HISTORY_EXPIRY = std::chrono::milliseconds(DMQ_SEQ_HISTORY_EXPIRY_MS);

    /// @brief Default node ID sent in every DmqHeader (0 = unset).
    /// Override via DMQ_NODE_ID in delegatemqconfig.h.
    static_assert(DMQ_NODE_ID >= 0 && DMQ_NODE_ID <= 255, "DMQ_NODE_ID must be 0-255");
    inline constexpr uint8_t NODE_ID = DMQ_NODE_ID;

    /// @brief Max number of pending messages the TransportMonitor can track.
    /// Override via DMQ_TRANSPORT_MONITOR_MAX_PENDING in delegatemqconfig.h.
    inline constexpr size_t MAX_TRANSPORT_MONITOR_PENDING = DMQ_TRANSPORT_MONITOR_MAX_PENDING;

    /// @brief Embedded UDP transports: datagram buffer size and receive timeouts.
    /// Override via DMQ_UDP_BUFFER_SIZE / DMQ_UDP_PUB_RECV_TIMEOUT_MS /
    /// DMQ_UDP_SUB_RECV_TIMEOUT_MS in delegatemqconfig.h.
    inline constexpr size_t UDP_BUFFER_SIZE = DMQ_UDP_BUFFER_SIZE;
    inline constexpr std::chrono::milliseconds UDP_PUB_RECV_TIMEOUT{ DMQ_UDP_PUB_RECV_TIMEOUT_MS };
    inline constexpr std::chrono::milliseconds UDP_SUB_RECV_TIMEOUT{ DMQ_UDP_SUB_RECV_TIMEOUT_MS };

    /// @brief STM32 UART transport sizes and timeouts.
    /// Override via DMQ_UART_RX_RING_SIZE / DMQ_UART_MAX_PAYLOAD /
    /// DMQ_UART_TX_TIMEOUT_MS / DMQ_UART_FRAME_TIMEOUT_MS in delegatemqconfig.h.
    inline constexpr size_t UART_RX_RING_SIZE = DMQ_UART_RX_RING_SIZE;
    inline constexpr size_t UART_MAX_PAYLOAD = DMQ_UART_MAX_PAYLOAD;
    inline constexpr std::chrono::milliseconds UART_TX_TIMEOUT{ DMQ_UART_TX_TIMEOUT_MS };
    inline constexpr std::chrono::milliseconds UART_FRAME_TIMEOUT{ DMQ_UART_FRAME_TIMEOUT_MS };

    // --- MUTEX / LOCK SELECTION ---
#if defined(DMQ_THREAD_STDLIB) || defined(DMQ_THREAD_WIN32) || defined(DMQ_THREAD_QT) || defined(DMQ_THREAD_POSIX)
    // Windows / Linux / macOS / Qt / POSIX
    using Mutex = std::mutex;
    using RecursiveMutex = std::recursive_mutex;
    // No ISR concept reachable from userspace on desktop OSes, and
    // Timer::ProcessTimers() is always driven from an ordinary thread here
    // in practice -- CriticalSection is just RecursiveMutex. See
    // ThreadXCriticalSection.h for what a "real" ISR-safe implementation
    // looks like on a port where it matters.
    using CriticalSection = RecursiveMutex;
    using ConditionVariable = std::condition_variable;
    template<typename T> using LockGuard = std::lock_guard<T>;
    template<typename T> using UniqueLock = std::unique_lock<T>;
    #define DMQ_HAS_CV
    #define DMQ_HAS_SEMAPHORE  // generic dmq::Semaphore (delegate/Semaphore.h) needs a real ConditionVariable

#elif defined(DMQ_THREAD_FREERTOS)
    // Use the custom FreeRTOS wrapper
    using Mutex = dmq::os::FreeRTOSMutex;
    using RecursiveMutex = dmq::os::FreeRTOSRecursiveMutex;
    // ISR-safe on real ARM Cortex-M FreeRTOS ports (auto-detects context via
    // xPortIsInsideInterrupt() and selects taskENTER_CRITICAL[_FROM_ISR]
    // accordingly); on the desktop simulator ports (POSIX/Win32) it is
    // task-context-only, since there is no real hardware interrupt to
    // detect there. See FreeRTOSCriticalSection.h for the detection details
    // and the __arm__/__ARM_ARCH scoping assumption.
    using CriticalSection = dmq::os::FreeRTOSCriticalSection;
    using ConditionVariable = dmq::os::FreeRTOSConditionVariable;
    template<typename T> using LockGuard = PortableLockGuard<T>;
    template<typename T> using UniqueLock = std::unique_lock<T>;
    #define DMQ_HAS_CV
    #define DMQ_HAS_SEMAPHORE  // generic dmq::Semaphore (delegate/Semaphore.h) needs a real ConditionVariable

#elif defined(DMQ_THREAD_THREADX)
    // Use the custom ThreadX wrapper
    using Mutex = dmq::os::ThreadXMutex;
    using RecursiveMutex = dmq::os::ThreadXRecursiveMutex;
    // Genuinely ISR-safe (interrupt masking, not a TX_MUTEX) -- see
    // ThreadXCriticalSection.h for why a real OS mutex can never be made
    // to work here, and for the narrow-use-only warnings that apply to it.
    using CriticalSection = dmq::os::ThreadXCriticalSection;
    using ConditionVariable = dmq::os::ThreadXConditionVariable;
    template<typename T> using LockGuard = PortableLockGuard<T>;
    template<typename T> using UniqueLock = std::unique_lock<T>;
    #define DMQ_HAS_CV
    #define DMQ_HAS_SEMAPHORE  // generic dmq::Semaphore (delegate/Semaphore.h) needs a real ConditionVariable

#elif defined(DMQ_THREAD_ZEPHYR)
    // Use the custom Zephyr wrapper
    using Mutex = dmq::os::ZephyrMutex;
    using RecursiveMutex = dmq::os::ZephyrRecursiveMutex;
    // ISR-safe (irq_lock()/irq_unlock(key), not a k_mutex) -- see
    // ZephyrCriticalSection.h.
    using CriticalSection = dmq::os::ZephyrCriticalSection;
    // No dmq::ConditionVariable port for Zephyr (no DMQ_HAS_CV), but
    // dmq::Semaphore is available via Zephyr's own native k_sem instead of
    // the generic condvar+mutex implementation -- see ZephyrSemaphore.h and
    // delegate/Semaphore.h. This is what makes DelegateAsyncWait available
    // here.
    using Semaphore = dmq::os::ZephyrSemaphore;
    #define DMQ_HAS_SEMAPHORE
    template<typename T> using LockGuard = PortableLockGuard<T>;

#elif defined(DMQ_THREAD_CMSIS_RTOS2)
    using Mutex = dmq::os::CmsisRtos2Mutex;
    using RecursiveMutex = dmq::os::CmsisRtos2RecursiveMutex;
    // ISR-safe (__disable_irq()/__enable_irq() via CMSIS-Core, bypassing
    // osMutex entirely) -- see CmsisRtos2CriticalSection.h. UNVERIFIED: no
    // CMSIS-RTOS2 SDK or Cortex-M hardware/QEMU target is available in this
    // development environment to build and run it; review before relying
    // on it in production.
    using CriticalSection = dmq::os::CmsisRtos2CriticalSection;
    // No dmq::ConditionVariable port for CMSIS-RTOS2 (no DMQ_HAS_CV, and no
    // native condvar primitive to build one from), but dmq::Semaphore is
    // available via osSemaphore directly instead -- see
    // CmsisRtos2Semaphore.h and delegate/Semaphore.h. This is what makes
    // DelegateAsyncWait available here.
    using Semaphore = dmq::os::CmsisRtos2Semaphore;
    #define DMQ_HAS_SEMAPHORE
    template<typename T> using LockGuard = PortableLockGuard<T>;

#elif defined(DMQ_THREAD_NUTTX)
    // Use the custom NuttX wrapper (NuttX implements real POSIX
    // pthread_mutex_t, unlike the bespoke kernel-object wrappers the other
    // RTOS ports need).
    using Mutex = dmq::os::NuttXMutex;
    using RecursiveMutex = dmq::os::NuttXRecursiveMutex;
    // ISR-safe (up_irq_save()/up_irq_restore(), NuttX's own architecture-
    // portable interrupt-masking primitive, not a pthread_mutex_t) -- see
    // NuttXCriticalSection.h, including its FLAT-vs-PROTECTED/KERNEL-build
    // caveat.
    using CriticalSection = dmq::os::NuttXCriticalSection;
    // No dmq::ConditionVariable port for NuttX (no DMQ_HAS_CV) -- not
    // because NuttX lacks pthread_cond_t (it has a real one), but because
    // dmq::Semaphore is available via NuttX's own native sem_t instead of
    // the generic condvar+mutex implementation -- see NuttXSemaphore.h and
    // delegate/Semaphore.h. This is what makes DelegateAsyncWait available
    // here.
    using Semaphore = dmq::os::NuttXSemaphore;
    #define DMQ_HAS_SEMAPHORE
    template<typename T> using LockGuard = PortableLockGuard<T>;

#else
    // Bare metal has no threads, so no locking is required.
    // NullMutex satisfies BasicLockable; PortableLockGuard compiles to nothing meaningful.
    struct NullMutex {
        void lock() {}
        void unlock() {}
    };
    using Mutex = NullMutex;
    using RecursiveMutex = NullMutex;
    // Unlike Mutex/RecursiveMutex above, NullMutex would NOT be correct here:
    // bare metal still has real hardware ISRs that can preempt the main
    // loop, even with no RTOS/threads. dmq::os::BareMetalCriticalSection
    // does real interrupt masking (save/disable/restore PRIMASK), the same
    // technique BareMetalClock.h already uses.
    using CriticalSection = dmq::os::BareMetalCriticalSection;
    template<typename T> using LockGuard = PortableLockGuard<T>;
    // No DMQ_HAS_SEMAPHORE — no RTOS means no native semaphore primitive to
    // build one from either; Semaphore and DelegateAsyncWait are unavailable
    // on bare metal.
#endif
}

// Detect if exceptions are disabled at the compiler level
#if !defined(__cpp_exceptions)
    #ifndef DMQ_ASSERTS
        #define DMQ_ASSERTS  // Force asserts if exceptions are off
    #endif
#endif

// @TODO: Select the desired software fault handling (see Port.cmake).
#ifdef DMQ_ASSERTS
    #include "extras/util/Fault.h"
    // Use assert error handling. Change assert to a different error 
    // handler as required by the target application.
    #define BAD_ALLOC() DMQ_ASSERT()
#else
    #include "extras/util/Fault.h"
    #include <new>
    // Use exception error handling
    #define BAD_ALLOC() throw std::bad_alloc()
#endif

// @TODO: Select the desired heap allocation (see Port.cmake).
// If DMQ_ASSERTS defined above, consider defining DMQ_ALLOCATOR to prevent 
// std::list usage within delegate library from throwing a std::bad_alloc 
// exception. The std_allocator calls assert if out of memory. 
// See master CMakeLists.txt for info on enabling the fixed-block allocator.
#ifdef DMQ_ALLOCATOR
    // Use stl_allocator fixed-block allocator for dynamic storage allocation
    #include "extras/allocator/xstring.h"
    #include "extras/allocator/xlist.h"
    #include "extras/allocator/xmap.h"
    #include "extras/allocator/xset.h"
    #include "extras/allocator/xqueue.h"
    #include "extras/allocator/xsstream.h"
    #include "extras/allocator/stl_allocator.h"
    #include "extras/allocator/xnew.h"
    #include "extras/allocator/xmake_shared.h"
#else
    #include <string>
    #include <list>
    #include <map>
    #include <set>
    #include <queue>
    #include <deque>
    #include <sstream>
    #include <memory>
    #include <utility>

    // Not using xallocator; define as nothing
    #undef XALLOCATOR
    #define XALLOCATOR

    namespace dmq {
        // Use default std::allocator for dynamic storage allocation
        template <typename T>
        using stl_allocator = std::allocator<T>;

        // An alias, like xmap/xset here and xlist under DMQ_ALLOCATOR, so code
        // overloaded on std::list (e.g. msg_serialize.h) accepts it in both builds.
        template <typename T, typename Alloc = stl_allocator<T>>
        using xlist = std::list<T, Alloc>;

        // stl_allocator is std::allocator here, so these are the standard types
        typedef std::ostringstream xostringstream;
        typedef std::stringstream xstringstream;

        typedef std::string xstring;

        // Fallback xmake_shared — uses std::make_shared when fixed-block allocator is disabled
        template <typename T, typename... Args>
        inline std::shared_ptr<T> xmake_shared(Args&&... args)
        {
            return std::make_shared<T>(std::forward<Args>(args)...);
        }

        // Fallback xnew/xdelete — use standard new/delete when fixed-block allocator is disabled
        template<typename T, typename... Args>
        inline T* xnew(Args&&... args) {
            return new(std::nothrow) T(std::forward<Args>(args)...);
        }

        template<typename T>
        inline void xdelete(T* p) {
            delete p;
        }

        // Fallback xmap/xmultimap — use std::map when fixed-block allocator is disabled
        template <typename Key, typename Value, typename Alloc = stl_allocator<std::pair<const Key, Value>>>
        using xmap = std::map<Key, Value, std::less<Key>, Alloc>;

        template <typename Key, typename Value, typename Alloc = stl_allocator<std::pair<const Key, Value>>>
        using xmultimap = std::multimap<Key, Value, std::less<Key>, Alloc>;

        // Fallback xset/xmultiset — use std::set when fixed-block allocator is disabled
        template <typename Key, typename Compare = std::less<Key>, typename Alloc = stl_allocator<Key>>
        using xset = std::set<Key, Compare, Alloc>;

        template <typename Key, typename Compare = std::less<Key>, typename Alloc = stl_allocator<Key>>
        using xmultiset = std::multiset<Key, Compare, Alloc>;

        // Fallback xqueue — use std::queue when fixed-block allocator is disabled
        template <typename T, typename Container = std::deque<T, stl_allocator<T>>>
        using xqueue = std::queue<T, Container>;
    }
#endif

// @TODO: Select the desired logging (see Port.cmake).
#ifdef DMQ_LOG
    #include <spdlog/spdlog.h>
    #define LOG_INFO(...)    spdlog::info(__VA_ARGS__)
    #define LOG_DEBUG(...)   spdlog::debug(__VA_ARGS__)
    #define LOG_ERROR(...)   spdlog::error(__VA_ARGS__)
#else
    // No-op macros when logging disabled
    #define LOG_INFO(...)    do {} while(0)
    #define LOG_DEBUG(...)   do {} while(0)
    #define LOG_ERROR(...)   do {} while(0)
#endif

// @TODO: Select the desired template optimization level.
// Enable DMQ_FORCE_OPTIMIZE_DEBUG in your build system (e.g. CMake) to force
// the compiler to aggressively inline and optimize DelegateMQ templates even
// during unoptimized Debug builds. This significantly reduces code bloat and
// call stack depth for variadic template unpacking without losing the ability
// to step-debug your application code.
//
// NOTE: This feature relies on GCC/Clang-specific pragmas. MSVC's #pragma optimize
// cannot elevate optimizations above the command-line (/Od) baseline and is mostly
// deprecated for x64 targets. Thus, this macro is a silent no-op on MSVC Windows builds.
// For GCC/Clang, this uses -Os (optimize for size) to maximize flash savings.
#ifdef DMQ_FORCE_OPTIMIZE_DEBUG
    #if defined(__GNUC__) || defined(__clang__)
        #define DMQ_OPTIMIZE_ON \
            _Pragma("GCC push_options") \
            _Pragma("GCC optimize (\"Os\")")
        #define DMQ_OPTIMIZE_OFF \
            _Pragma("GCC pop_options")
    #else
        #define DMQ_OPTIMIZE_ON
        #define DMQ_OPTIMIZE_OFF
    #endif
#else
    #define DMQ_OPTIMIZE_ON
    #define DMQ_OPTIMIZE_OFF
#endif

#endif // _DELEGATE_OPT_H
