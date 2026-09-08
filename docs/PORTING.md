# Porting Guide

Numerous predefined platforms are already supported — Windows, Linux, FreeRTOS, ARM bare-metal, ThreadX, Zephyr, CMSIS-RTOS2, and Qt. Ready-made plugins for threading and communication interfaces exist, or you can create new ones.

---

## Table of Contents

- [Table of Contents](#table-of-contents)
- [Porting Checklist](#porting-checklist)
- [Embedded Systems](#embedded-systems)
- [Interfaces](#interfaces)
  - [`dmq::IThread`](#dmqithread)
    - [Send `dmq::DelegateMsg`](#send-dmqdelegatemsg)
    - [Receive `dmq::DelegateMsg`](#receive-dmqdelegatemsg)
  - [`dmq::ISerializer`](#dmqiserializer)
  - [`dmq::IDispatcher`](#dmqidispatcher)
- [Thread Implementations](#thread-implementations)
  - [Thread Priority and Latency](#thread-priority-and-latency)
  - [Message Queueing](#message-queueing)
  - [Watchdog Integration](#watchdog-integration)

---

## Porting Checklist

1. **Search codebase for `@TODO`** — find specific decision locations tagged in the source files.
2. **Implement `dmq::IThread`** — required to use **Asynchronous** delegates.
3. **Implement `dmq::ISerializer` and `dmq::IDispatcher`** — required to use **Remote** delegates across processes/processors.
   - *Note:* If using the **DataBus** (DDS Lite), you instead implement `dmq::transport::ITransport` which is the transport interface used by the DataBus `Participant` class.
   - *Optional:* Implement `dmq::transport::ITransportMonitor` if your application layer requires command acknowledgments (ACKs).
   - See [Sample Projects](../example/sample-projects/README.md) for numerous remote delegate examples.
4. **Check System Clock** — ensure `std::chrono::steady_clock` is supported on your target hardware, as it is required for timers and transport timeouts. Otherwise, change `dmq::Clock` in `DelegateOpt.h` to a new clock type.
5. **Call `dmq::util::Timer::ProcessTimers()`** — periodically call `ProcessTimers()` (e.g., from a main loop or hardware timer ISR) to support timers and thread watchdogs.
6. **Configure Build Options** — set CMake DMQ library build options within `CMakeLists.txt`.
   - Example: `DMQ_ASSERTS` for debug assertions.
   - Example: `DMQ_ALLOCATOR` to switch between standard heap (`new`/`delete`) and the deterministic Fixed Block Allocator.
   - Example: copy `DelegateMQConfig_Template.h` to your project and set `-DDMQ_USER_CONFIG="DelegateMQConfig.h"` to tune numeric library constants (`DMQ_MAX_TIMER_EXPIRED`, `DMQ_MAX_WATCHDOG_THREADS`, `DMQ_DEFAULT_QUEUE_SIZE`, etc.) without editing library files. See `delegate/DelegateMQConfig_Default.h` for all available constants and their default values.
7. **Implement Fault Handling** — customize `port/fault/Fault.cpp` to route errors to your system's logger or crash handler. This file is intentionally in the `port/` directory so it can be freely edited without touching the library source.

---

## Embedded Systems

Running C++ messaging on embedded targets (like STM32) requires specific attention to resources.

1. **Stack Usage & Debug Mode**

   **Issue:** In Debug mode (`-O0`), C++ templates generate deep call stacks, possibly causing stack overflows.  
   **Fix:** Increase task stack (e.g. 8 KB) or use Release mode (`-O2`), where stacks shrink significantly.

2. **Transport Implementation (if using remote delegates)**

   **Issue:** Blocking UART calls (e.g., `HAL_UART_Receive`) starve high-priority tasks.  
   **Fix:** Use an interrupt-driven ring buffer. The ISR captures data immediately, and the receive task sleeps on a semaphore until data is available.

See the `stm32-freertos` example `README.md` for a complete implementation of static stacks, interrupt-driven UART, and correct FreeRTOS configuration.

---

## Interfaces

DelegateMQ interface classes allow customizing the library's runtime behavior.

### `dmq::IThread`

The `dmq::IThread` interface sends a delegate and its argument data through a message queue. A delegate thread is required to dispatch asynchronous delegates to a specified target thread. The delegate library automatically constructs a `dmq::DelegateMsg` containing everything necessary for the destination thread.

```cpp
class IThread
{
public:
    virtual ~IThread() = default;

    /// Dispatch a DelegateMsg onto this thread. The implementer is responsible
    /// for getting the DelegateMsg into an OS message queue. Once DelegateMsg
    /// is on the correct thread of control, the IThreadInvoker::Invoke() function
    /// must be called to execute the delegate.
    /// @param[in] msg - a pointer to the delegate message that must be created dynamically.
    /// @pre Caller *must* create the DelegateMsg argument dynamically.
    /// @post The destination thread calls Invoke().
    /// @return true if the message was successfully enqueued, false otherwise.
    virtual bool DispatchDelegate(std::shared_ptr<DelegateMsg> msg) = 0;

    /// Returns true if the calling thread is this thread.
    /// @return true if the calling thread is this thread, false otherwise.
    virtual bool IsCurrentThread() = 0;
};
```

#### Send `dmq::DelegateMsg`

An asynchronous delegate's `operator()(Args... args)` calls `DispatchDelegate()` to send a delegate message to the destination thread.

```cpp
auto thread = this->GetThread();
if (thread) {
    // Dispatch message onto the callback destination thread. Invoke()
    // will be called by the destination thread.
    thread->DispatchDelegate(msg);
}
```

`DispatchDelegate()` inserts a message into the thread's message queue. Name your concrete class `<Platform>Thread` (e.g. `FreeRTOSThread`, matching the existing `StdlibThread`, `Win32Thread`, `ThreadXThread`, `ZephyrThread`, `CmsisRtos2Thread`, `QtThread` ports) in a `<Platform>Thread.h`/`.cpp` pair under `port/os/<platform>/`, and add `using Thread = <Platform>Thread;` inside `namespace dmq::os` at the end of the header — this is what lets application code, tests, and the rest of the library keep referring to `dmq::os::Thread` regardless of which port is active. Create a unique `DispatchDelegate()` implementation based on your platform's OS API.

The message type itself, `dmq::os::ThreadMsg`, is a single shared file — `port/os/common/ThreadMsg.h` — used by every port except Qt (which dispatches through Qt's own signal/slot queued-connection mechanism instead). Don't create a per-port copy; `#include "port/os/common/ThreadMsg.h"` from your new port's header.

The implementation should handle the queue-full case according to `dmq::FullPolicy` (defined once in `DelegateOpt.h`; each port aliases it as `using FullPolicy = dmq::FullPolicy;` inside `namespace dmq::os`, so port code just says `FullPolicy` unqualified) before allocating the message. On platforms with a lockable queue (stdlib, Win32), check first then allocate to avoid wasting heap on drops. On RTOS platforms (FreeRTOS, Zephyr, etc.) the OS queue API is atomic, so allocate first and delete on failure.

If your target's native queue API needs more than a couple of calls (buffer sizing, alignment, a create/destroy pair), isolate it behind a small RAII wrapper class — e.g. `port/os/freertos/FreeRTOSDelegateQueue.h` — rather than inlining native calls directly in `DispatchDelegate()`/`Run()`. The wrapper should expose `Create()`/`IsCreated()`/`Send(msg, highPriority, timeout)`/`Receive(timeout)`/`Size()`/`DrainAndDelete()`/`Destroy()` and own no policy decisions itself — `FullPolicy`, timeout computation, watchdog, and stats stay in `<Platform>Thread.cpp`. Use `FreeRTOSDelegateQueue.h`, `ThreadXDelegateQueue.h`, `CmsisRtos2DelegateQueue.h` as direct models; `ZephyrDelegateQueue.h` additionally shows the pattern for a native queue with no priority-send primitive (two queues, high-priority drained first):

```cpp
// stdlib / Win32 style — check under lock before allocating
void StdlibThread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    std::unique_lock<std::mutex> lk(m_mutex);

    if (MAX_QUEUE_SIZE > 0 && m_queue.size() >= MAX_QUEUE_SIZE)
    {
        if (FULL_POLICY == FullPolicy::DROP)
            return;  // discard — no allocation wasted

        // BLOCK: wait until consumer drains a slot
        m_cvNotFull.wait(lk, [this]() {
            return m_queue.size() < MAX_QUEUE_SIZE || m_exit.load();
        });
    }

    auto threadMsg = std::make_shared<ThreadMsg>(MSG_DISPATCH_DELEGATE, msg);
    m_queue.push(threadMsg);
    m_cv.notify_one();
}

// RTOS style (e.g. FreeRTOS) — allocate first, let the queue wrapper enforce the limit
bool FreeRTOSThread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    ThreadMsg* threadMsg = new (std::nothrow) ThreadMsg(MSG_DISPATCH_DELEGATE, msg);
    if (!threadMsg) return false;

    TickType_t timeout = (FULL_POLICY == FullPolicy::DROP) ? 0 : portMAX_DELAY;
    if (!m_queue.Send(threadMsg, msg->GetPriority() == dmq::Priority::HIGH, timeout))
    {
        delete threadMsg;  // queue full, dropped per policy
        return false;
    }
    return true;
}
```

#### Receive `dmq::DelegateMsg`

Inherit from `dmq::IThreadInvoker` and implement the `Invoke()` function. The destination thread calls `Invoke()` once the message is dequeued.

```cpp
/// @brief Abstract base class to support asynchronous delegate function invoke
/// on destination thread of control.
class IThreadInvoker
{
public:
    virtual ~IThreadInvoker() = default;

    /// Called to invoke the bound target function by the destination thread of control.
    /// @param[in] msg - the incoming delegate message.
    /// @return `true` if function was invoked; `false` if failed.
    virtual bool Invoke(std::shared_ptr<DelegateMsg> msg) = 0;
};
```

The `dmq::os::Thread::Process()` loop below shows the dispatch pattern. `Invoke()` is called for each incoming `MSG_DISPATCH_DELEGATE` queue message.

```cpp
void Thread::Process()
{
    // ...
            case MSG_DISPATCH_DELEGATE:
            {
                auto delegateMsg = msg->GetData();
                ASSERT_TRUE(delegateMsg);

                auto invoker = delegateMsg->GetInvoker();
                ASSERT_TRUE(invoker);

                bool success = invoker->Invoke(delegateMsg);
                ASSERT_TRUE(success);
                break;
            }

            case MSG_TIMER:
                // Call ProcessTimers() to service all active dmq::util::Timer instances
                dmq::util::Timer::ProcessTimers();
                break;
    // ...
}
```

---

### `dmq::ISerializer`

The `dmq::ISerializer` interface serializes argument data for sending to a remote destination endpoint. Each argument is serialized and deserialized according to system requirements. Custom serialization libraries such as [MessagePack](https://msgpack.org/index.html) are supported. The `examples/sample-projects` folder contains working examples.

```cpp
template <class R>
struct ISerializer; // Not defined

/// @brief Delegate serializer interface for serializing and deserializing
/// remote delegate arguments. Implemented by application code if remote
/// delegates are used.
///
/// @details All argument data is serialized into a stream. `Write()` is called
/// by the sender when the delegate is invoked. `Read()` is called by the receiver
/// upon reception of the remote message data bytes.
template<class RetType, class... Args>
class ISerializer<RetType(Args...)>
{
public:
    /// Serialize data for transport.
    /// @param[out] os The output stream
    /// @param[in] args The target function arguments
    /// @return The output stream
    virtual std::ostream& Write(std::ostream& os, const Args&... args) = 0;

    /// Deserialize data from transport.
    /// @param[in] is The input stream
    /// @param[out] args The target function arguments
    /// @return The input stream
    virtual std::istream& Read(std::istream& is, Args&... args) = 0;
};
```

---

### `dmq::IDispatcher`

The `dmq::IDispatcher` interface dispatches serialized argument data to a remote destination endpoint. Custom dispatchers using sockets, named pipes, ZeroMQ, or any other transport are supported. The `examples/sample-projects` folder contains working examples.

```cpp
/// @brief Delegate interface class to dispatch serialized function argument data
/// to a remote destination. Implemented by the application if using remote delegates.
///
/// @details Incoming data from the remote must call `IDispatcher::Dispatch()` to
/// invoke the target function using argument data. The argument data is serialized
/// for transport using a concrete class implementing the `ISerializer` interface,
/// allowing any serialization method to be used.
/// @post The receiver calls `IRemoteInvoker::Invoke()` when the dispatched message
/// is received.
class IDispatcher
{
public:
    virtual ~IDispatcher() = default;

    /// Dispatch a stream of bytes to a remote system. The implementer is responsible
    /// for sending the bytes over a communication transport (UDP, TCP, shared memory,
    /// serial, ...).
    /// @param[in] os An outgoing stream to send to the remote destination.
    /// @param[in] id The unique delegate identifier shared between sender and receiver.
    virtual int Dispatch(dmq::xostringstream& os, DelegateRemoteId id, uint16_t* outSeqNum = nullptr) = 0;
};
```

---

## Thread Implementations

While DelegateMQ provides the `dmq::IThread` interface, the library includes concrete `dmq::os::Thread` class implementations for many OSs (`StdlibThread`, `Win32Thread`, `FreeRTOSThread`, `ThreadXThread`, `ZephyrThread`, `CmsisRtos2Thread`, `QtThread` — each aliased as `dmq::os::Thread`). These implementations provide a standard event loop and several advanced features for robustness and flow control.

### Thread Priority and Latency

When a delegate is dispatched to a worker thread, it is posted to that thread's message queue. The thread's OS priority determines how quickly the callback executes relative to other tasks.

- **High Priority**: Use for time-critical delegates (e.g., safety-critical commands, emergency stops).
- **Medium Priority**: Suitable for general application logic.
- **Low Priority**: Best for non-critical tasks like background telemetry or logging.

The end-to-end dispatch latency is primarily dominated by the destination thread's priority and the OS scheduler wake-up time.

### Message Queueing

High-priority delegate messages jump to the front of the line, but the mechanism is port-specific: FreeRTOS/ThreadX send to the front of their native queue; CMSIS-RTOS2 passes priority directly via `osMessageQueuePut`'s `msg_prio` argument; stdlib/Win32/Qt keep two queues (high, normal) and always drain high first; Zephyr's `k_msgq` has no native priority-send, so it also uses the two-queue approach (see `ZephyrDelegateQueue.h`).

#### dmq::FullPolicy (Back Pressure / Drop)

When a thread's message queue has a fixed size (`maxQueueSize > 0`), `dmq::FullPolicy` controls what `DispatchDelegate()` does when the queue is full. It's defined once in `DelegateOpt.h`; every port aliases it as `dmq::os::FullPolicy` for source compatibility, so either name works. The default is `FullPolicy::FAULT`.

```cpp
// Fault on overflow — appropriate for threads that must never silently lose messages
dmq::os::Thread cmdThread("CmdThread", /*maxQueueSize=*/50, dmq::FullPolicy::FAULT);

// Wait up to 2 s for the consumer to drain a slot, then log a warning and drop
dmq::os::Thread safetyThread("SafetyThread", /*maxQueueSize=*/50, dmq::FullPolicy::TIMEOUT);

// Drop stale samples rather than stall the publisher
dmq::os::Thread sensorThread("SensorThread", /*maxQueueSize=*/10, dmq::FullPolicy::DROP);
```

- **`FullPolicy::FAULT`** *(default)*: `DispatchDelegate()` triggers a system fault if the queue is full. This makes overflow immediately visible during development and integration testing rather than silently degrading. Use when a full queue indicates a design error (producer outrunning consumer) that should not be masked.
- **`FullPolicy::TIMEOUT`**: `DispatchDelegate()` waits up to `dispatchTimeout` (default `dmq::DEFAULT_DISPATCH_TIMEOUT` = 2 s) for the consumer to drain a slot, then logs a warning and drops the message. This provides bounded back pressure — the publisher is held briefly during transient bursts but is never stalled indefinitely. Use for critical topics (commands, state transitions) where every message should be delivered if possible but unbounded blocking is unacceptable. Choose a timeout shorter than the watchdog timeout so stalls are detected and reported.
- **`FullPolicy::DROP`**: `DispatchDelegate()` silently discards the message and returns immediately without stalling the caller. Ideal for high-rate best-effort data (sensor telemetry, display updates) where a stale sample is preferable to stalling the publisher.

Setting `maxQueueSize = 0` disables the limit entirely — `FullPolicy` has no effect and all messages are queued regardless of consumer speed.

`FullPolicy` is a thread-level setting. All delegates dispatched to the same `dmq::os::Thread` instance share the policy. If a single thread serves both drop-tolerant and loss-intolerant subscribers, split them across separate threads with different policies.

### Watchdog Integration

All `dmq::os::Thread` port implementations (stdlib, Win32, FreeRTOS, CMSIS-RTOS2, ThreadX, Zephyr, Qt) support an optional watchdog. Enable it by passing a timeout to `CreateThread()`:

```cpp
thread.CreateThread(std::chrono::seconds(2));  // fault if thread stalls > 2s
```

The mechanism uses a single `dmq::util::Timer` object — no additional OS threads are created:

- **`m_watchdogTimer`** (fires every `timeout/2`): calls `WatchdogCheck()` synchronously in the `dmq::util::Timer::ProcessTimers()` caller context. `WatchdogCheck()` reads `m_lastAliveTime` atomically. If `now − m_lastAliveTime > timeout`, a fault is triggered.

The worker thread's `Run()` loop updates `m_lastAliveTime` directly at the top of every iteration. When a watchdog timeout is configured, the queue-receive call uses a finite wait of `timeout/4`, so the loop cycles and refreshes the timestamp even when the thread is completely idle. No queue messages are needed to maintain liveness — there is no cross-thread dispatch involved in the watchdog path.

```
ProcessTimers() caller (ISR or high-priority task)
   │
   └─ m_watchdogTimer fires (timeout/2)
        └─ WatchdogCheck() runs inline — reads m_lastAliveTime atomically
             └─ gap > timeout → trigger fault/recovery

worker Run() loop
   top-of-loop: m_lastAliveTime = now           ← refreshed every iteration
   queue receive (blocks at most timeout/4)      ← finite wait; loops even when idle
   process message (or wake on timeout, loop back)
```

#### Priority Requirement — Critical on Single-Core RTOS

The watchdog **only catches CPU-spinning runaway threads** if `dmq::util::Timer::ProcessTimers()` runs at a **higher priority** than the threads it watches. On a single-core RTOS (FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2), a runaway high-priority task starves all lower-priority tasks including the one calling `ProcessTimers()` — the watchdog timers never fire and the fault is never detected.

| Failure mode | ProcessTimers() priority requirement |
|:---|:---|
| Deadlocked thread (blocked on mutex/semaphore) | Any — blocked threads don't consume CPU |
| Idle thread (waiting for messages) | Any — `Run()` loops every `timeout/4` even with no messages |
| CPU-spinning runaway (infinite loop in callback) | **Must be higher** than the watched thread |

Two acceptable approaches for embedded targets:

**Option 1 — Hardware timer ISR (recommended):** Call `dmq::util::Timer::ProcessTimers()` from a SysTick or similar periodic ISR. ISRs preempt all tasks regardless of priority, so the watchdog fires even if the highest-priority task runs away.

```cpp
// FreeRTOS / CMSIS-RTOS2 example: SysTick ISR
extern "C" void SysTick_Handler(void)
{
    dmq::util::Timer::ProcessTimers();  // preempts all tasks — watchdog always fires
    // ... other SysTick work
}
```

**Option 2 — Highest-priority task:** Dedicate the system's highest-priority task to calling `dmq::util::Timer::ProcessTimers()` in a tight loop. This task must be strictly higher priority than any thread it is watching — a runaway task at equal or lower priority will starve it and defeat the watchdog.

```cpp
// FreeRTOS example: dedicated watchdog task at highest priority
void WatchdogTask(void*)
{
    while (true)
    {
        dmq::util::Timer::ProcessTimers();
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms tick rate
    }
}

// At startup — assign priority above all worker threads
xTaskCreate(WatchdogTask, "Watchdog", 512, nullptr, configMAX_PRIORITIES - 1, nullptr);
```

#### Watchdog Limitations

**1. Long-running message handlers.**  
`Run()` updates `m_lastAliveTime` only at the top of each iteration, before blocking on the queue receive. If a message handler runs longer than `watchdogTimeout`, the watchdog fires — this is the correct behavior (the thread is unresponsive to new messages during that time). If a handler is legitimately long (e.g., a blocking flash write or a slow I/O call), either:
- Call `ThreadCheck()` periodically inside the handler to reset the alive timestamp, or
- Set `watchdogTimeout` large enough to cover the worst-case handler duration.

**2. `ProcessTimers()` context must remain alive.**  
`WatchdogCheck()` runs in whichever context calls `dmq::util::Timer::ProcessTimers()`. If that context itself deadlocks, starves, or is never scheduled, the watchdog timer never fires and no fault is detected. Mitigations:
- Call `ProcessTimers()` from a hardware timer ISR (e.g., SysTick) so it preempts all tasks unconditionally.
- For a final backstop against total system freeze, pair the software watchdog with a hardware watchdog (e.g., STM32 IWDG) kicked inside `WatchdogCheck()`. The software watchdog detects soft stalls; the hardware watchdog catches a completely frozen `ProcessTimers()` context.

**3. Priority requirement on single-core RTOS (see table above).**  
For CPU-spinning runaways, `ProcessTimers()` must run at a higher priority than the watched thread. A deadlocked or idle thread does not require elevated priority because it yields the CPU voluntarily.

### Performance Monitoring

Every `dmq::os::Thread` port supports high-resolution performance monitoring. This requires three pieces of instrumentation in the port layer:

1. **Enqueue Timestamp**: The shared `ThreadMsg` class (`port/os/common/ThreadMsg.h`) already captures a `dmq::TimePoint` (steady clock) via `SetEnqueueTime()`/`GetEnqueueTime()` under `DMQ_DATABUS_TOOLS` — call `SetEnqueueTime(Timer::GetNow())` right after constructing each message in your `DispatchDelegate()`.
2. **Latency Calculation**: In the thread's `Run()` loop, calculate the "Queue Latency" (Dispatch Time - Enqueue Time) just before invoking the delegate.
3. **`SnapshotStats()` Implementation**: Implement the `SnapshotStats()` method to return an atomic snapshot of windowed and all-time statistics.

```cpp
#if defined(DMQ_DATABUS_TOOLS)
Thread::ThreadStats Thread::SnapshotStats()
{
    ThreadStats stats;
    stats.cpu_name = CPU_NAME;
    stats.thread_name = THREAD_NAME;

    std::unique_lock<std::mutex> lk(m_mutex);
    stats.queue_depth = m_queue.size();
    stats.queue_depth_max_window = m_queueDepthMaxWindow;
    stats.queue_depth_max_all = m_queueDepthMaxAll;
    stats.queue_size_limit = MAX_QUEUE_SIZE;

    // Windowed average and max latency
    if (m_latencyCountWindow > 0)
        stats.latency_avg_ms = (float)m_latencyTotalWindow.count() / m_latencyCountWindow;
    else
        stats.latency_avg_ms = 0;

    stats.latency_max_window_ms = (float)m_latencyMaxWindow.count();
    stats.latency_max_all_ms = (float)m_latencyMaxAll.count();
    stats.dispatch_count = m_dispatchCountAll;

    // Reset windowed counters
    m_queueDepthMaxWindow = stats.queue_depth;
    m_latencyTotalWindow = dmq::Duration(0);
    m_latencyCountWindow = 0;
    m_latencyMaxWindow = dmq::Duration(0);

    return stats;
}
#endif
```

The windowed counters (`m_queueDepthMaxWindow`, `m_latencyMaxWindow`, etc.) must be updated during every `DispatchDelegate()` and message processing iteration under the same internal lock used by `SnapshotStats()`.
