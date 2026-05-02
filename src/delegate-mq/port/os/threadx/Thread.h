#ifndef _THREAD_THREADX_H
#define _THREAD_THREADX_H

/// @file Thread.h
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2026.
///
/// @brief ThreadX implementation of the DelegateMQ IThread interface.
///
/// @details
/// This class provides a concrete implementation of the `IThread` interface using 
/// Azure RTOS ThreadX primitives. It enables DelegateMQ to dispatch asynchronous 
/// delegates to a dedicated ThreadX thread.
///
/// @note This implementation is a basic port. For reference, the stdlib and win32
/// implementations provide additional features:
/// 1. Synchronized Startup: CreateThread() blocks until the worker thread is ready.
///
/// **Key Features:**
/// * **Task Integration:** Wraps `tx_thread_create` to establish a dedicated worker loop.
/// * **FullPolicy Support:** Configurable back-pressure (DROP or TIMEOUT) when the
///   message queue is full.
/// * **Priority Support:** Normal and High priorities (uses `tx_queue_front_send`).
/// * **Queue-Based Dispatch:** Uses a `TX_QUEUE` to receive and process incoming
///   delegate messages in a thread-safe manner.
/// * **Priority Control:** Supports runtime priority configuration via `SetThreadPriority`.
/// * **Dynamic Configuration:** Allows configuring stack size and queue depth at construction.
/// * **Graceful Shutdown:** Implements robust termination logic using semaphores to ensure
///   the thread exits cleanly before destruction.
/// * **Watchdog Integration:** Optional heartbeat mechanism detects stalled or deadlocked
///   threads. Enable by passing a timeout to CreateThread(). Requires
///   Timer::ProcessTimers() to be called from a context that can preempt watched threads
///   — typically a hardware timer ISR or the highest-priority task in the system.

#include "delegate/IThread.h"
#include "extras/util/Timer.h"
#include "tx_api.h"
#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <optional>

namespace dmq::os {

class ThreadMsg;

/// @brief Policy applied when the thread message queue is full.
/// @details Only meaningful when maxQueueSize > 0.
///   - DROP:    DispatchDelegate() silently discards the message and returns immediately.
///   - FAULT:   DispatchDelegate() triggers a system fault if the queue is full.
///   - TIMEOUT: DispatchDelegate() waits up to dispatchTimeout, then logs and drops.
///
/// Use DROP for high-rate best-effort topics (sensor telemetry, display updates) where
/// a stale sample is preferable to stalling the publisher. FAULT is the default.
enum class FullPolicy { DROP, FAULT, TIMEOUT };

class Thread : public dmq::IThread
{
public:
    /// Default queue size if 0 is passed
    static const ULONG DEFAULT_QUEUE_SIZE = 20;

    /// Constructor
    /// @param threadName Name for the ThreadX thread
    /// @param maxQueueSize Max number of messages in queue (0 = Default 20)
    /// @param fullPolicy Action when queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    Thread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT);

    /// Destructor
    ~Thread();

    /// Called once to create the worker thread. If watchdogTimeout value
    /// provided, the maximum watchdog interval is used. Otherwise no watchdog.
    /// @param[in] watchdogTimeout - optional watchdog timeout.
    /// @return TRUE if thread is created. FALSE otherwise.
    bool CreateThread(std::optional<dmq::Duration> watchdogTimeout = std::nullopt);

    /// Terminate the thread gracefully
    void ExitThread();

    /// Get the ID of this thread instance
    TX_THREAD* GetThreadId();

    /// Get the ID of the currently executing thread
    static TX_THREAD* GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    /// Set the ThreadX Priority (0 = Highest). 
    /// Can be called before or after CreateThread().
    void SetThreadPriority(UINT priority);

    /// Get current priority
    UINT GetThreadPriority();

    /// Get thread name
    std::string GetThreadName() { return THREAD_NAME; }

    /// Get current queue size
    size_t GetQueueSize();

    /// Sleep for a duration.
    /// @param[in] timeout - the duration to sleep.
    static void Sleep(dmq::Duration timeout);

    // IThread Interface Implementation
    virtual bool DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg) override;

private:
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    /// Entry point for the thread
    static void Process(ULONG instance);

    // Run loop called by Process
    void Run();

    /// Check watchdog is expired. Called from Timer::ProcessTimers() context.
    void WatchdogCheck();

    /// @brief Manually update the watchdog alive timestamp.
    /// @details The Run() loop refreshes the timestamp automatically on every iteration.
    /// Call this from inside long-running message handlers to prevent a false watchdog
    /// alarm when a handler legitimately takes longer than watchdogTimeout.
    void ThreadCheck();

    const std::string THREAD_NAME;
    const size_t m_queueSize; // Stored queue size
    const FullPolicy FULL_POLICY;
    const dmq::Duration m_dispatchTimeout;
    UINT m_priority;    // Stored priority

    // ThreadX Control Blocks
    TX_THREAD m_thread;
    TX_QUEUE m_queue;
    TX_SEMAPHORE m_exitSem; // Semaphore to signal thread completion
    std::atomic<bool> m_exit = false;

    // Memory buffers required by ThreadX (Managed by RAII)
    // Using ULONG[] ensures correct alignment for ThreadX stacks and queues
    std::unique_ptr<ULONG[]> m_stackMemory;
    std::unique_ptr<ULONG[]> m_queueMemory;

    // Configurable stack size (bytes)
    static const ULONG STACK_SIZE = 2048;

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::unique_ptr<Timer> m_watchdogTimer;
    dmq::ScopedConnection m_watchdogTimerConn;
    std::atomic<dmq::Duration> m_watchdogTimeout;
};

} // namespace dmq::os

#endif // _THREAD_THREADX_H