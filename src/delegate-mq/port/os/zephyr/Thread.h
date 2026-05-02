#ifndef _THREAD_ZEPHYR_H
#define _THREAD_ZEPHYR_H

/// @file Thread.h
/// @brief Zephyr RTOS implementation of the DelegateMQ IThread interface.
///
/// @note This implementation is a basic port. For reference, the stdlib and win32
/// implementations provide additional features:
/// 1. Priority Support: Uses a priority queue to respect dmq::Priority.
/// 2. Synchronized Startup: CreateThread() blocks until the worker thread is ready.
///
/// **Key Features:**
/// * **Task Integration:** Wraps `k_thread_create` to establish a dedicated worker loop.
/// * **FullPolicy Support:** Configurable back-pressure (DROP or TIMEOUT) when the
///   message queue is full.
/// * **Queue-Based Dispatch:** Uses `k_msgq` to receive and process incoming
///   delegate messages in a thread-safe manner.
/// * **Watchdog Integration:** Optional heartbeat mechanism detects stalled or deadlocked
///   threads. Enable by passing a timeout to CreateThread(). Requires
///   Timer::ProcessTimers() to be called from a context that can preempt watched threads
///   — typically a hardware timer ISR or the highest-priority task in the system.
///
#include "delegate/IThread.h"
#include "extras/util/Timer.h"
#include <zephyr/kernel.h>
#include <string>
#include <memory>
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
/// a stale sample is preferable to stalling the publisher. Use TIMEOUT for critical topics
/// (commands, state transitions) where every message should be delivered if possible.
/// FAULT is the default.
enum class FullPolicy { DROP, FAULT, TIMEOUT };

class Thread : public dmq::IThread
{
public:
    /// Default queue size if 0 is passed
    static const size_t DEFAULT_QUEUE_SIZE = 20;

    /// Constructor
    /// @param threadName Name for the Zephyr thread
    /// @param maxQueueSize Max number of messages in queue (0 = Default 20)
    /// @param fullPolicy Action when queue is full: FAULT (default), DROP, or TIMEOUT.
    /// @param dispatchTimeout Duration to wait before giving up when policy is TIMEOUT.
    Thread(const std::string& threadName, size_t maxQueueSize = 0, FullPolicy fullPolicy = FullPolicy::FAULT,
           dmq::Duration dispatchTimeout = dmq::DEFAULT_DISPATCH_TIMEOUT);
    ~Thread();

    /// Called once to create the worker thread. If watchdogTimeout value
    /// provided, the maximum watchdog interval is used. Otherwise no watchdog.
    /// @param[in] watchdogTimeout - optional watchdog timeout.
    /// @return TRUE if thread is created. FALSE otherwise.
    bool CreateThread(std::optional<dmq::Duration> watchdogTimeout = std::nullopt);
    void ExitThread();

    // Note: k_tid_t is a struct k_thread* in Zephyr
    k_tid_t GetThreadId();
    static k_tid_t GetCurrentThreadId();

    /// Returns true if the calling thread is this thread
    virtual bool IsCurrentThread() override;

    /// Set the Zephyr Priority.
    /// Can be called before or after CreateThread().
    void SetThreadPriority(int priority);

    std::string GetThreadName() { return THREAD_NAME; }

    /// Get current queue size
    size_t GetQueueSize();

    /// Sleep for a duration.
    /// @param[in] timeout - the duration to sleep.
    static void Sleep(dmq::Duration timeout);

    virtual bool DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg) override;

private:
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    // Thread entry point
    static void Process(void* p1, void* p2, void* p3);
    void Run();

    /// Check watchdog is expired. Called from Timer::ProcessTimers() context.
    void WatchdogCheck();

    /// @brief Manually update the watchdog alive timestamp.
    /// @details The Run() loop refreshes the timestamp automatically on every iteration.
    /// Call this from inside long-running message handlers to prevent a false watchdog
    /// alarm when a handler legitimately takes longer than watchdogTimeout.
    void ThreadCheck();

    const std::string THREAD_NAME;
    const size_t m_queueSize;
    const FullPolicy FULL_POLICY;
    const dmq::Duration m_dispatchTimeout;
    int m_priority;

    // Zephyr Kernel Objects
    struct k_thread m_thread;
    struct k_msgq m_msgq;
    struct k_sem m_exitSem; // Semaphore to signal thread completion
    std::atomic<bool> m_exit = false;

    // Define pointer type for the message queue
    using MsgPtr = ThreadMsg*;

    // Custom deleter for Zephyr kernel memory (wraps k_free)
    using ZephyrDeleter = void(*)(void*);

    // Dynamically allocated stack and message queue buffer
    // Managed by unique_ptr but allocated via k_aligned_alloc and freed via k_free
    std::unique_ptr<char, ZephyrDeleter> m_stackMemory{nullptr, k_free};
    std::unique_ptr<char, ZephyrDeleter> m_msgqBuffer{nullptr, k_free};

    // Stack size in bytes
    static const size_t STACK_SIZE = 2048;
    // Size of one message item (the pointer)
    static const size_t MSG_SIZE = sizeof(MsgPtr);

    // Watchdog related members
    std::atomic<dmq::TimePoint> m_lastAliveTime;
    std::unique_ptr<dmq::util::Timer> m_watchdogTimer;
    dmq::ScopedConnection m_watchdogTimerConn;
    std::atomic<dmq::Duration> m_watchdogTimeout;
};

} // namespace dmq::os

#endif // _THREAD_ZEPHYR_H